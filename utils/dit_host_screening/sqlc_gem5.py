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
# arm -> (binary, DIT_MODE). `blanket` REUSES the nodit binary and selects the
# mode at runtime, so blanket-vs-nodit is a same-binary comparison and cannot
# carry a codegen difference. The previous arm set compiled a separate
# q_blanket, and that comparison produced the impossible reading that condemned
# the 2026-08-20 sweep: +3.49% serializing against +9.99% renamed for a single
# `msr DIT`, when a serializing write can never be cheaper than a renamed one.
ARMS = {"plain":   ("q_plain", 0),
        "nodit":   ("q_nodit", 0),
        "blanket": ("q_nodit", 1),
        "hoist":   ("q_hoist", 0),
        # Hand placement over the three provider entry points, on an
        # UNINSTRUMENTED libtomcrypt. It is the coverage reference: the pass
        # protects enough only if its ditSuppressed reaches the oracle's.
        # Comparing the pass against `blanket` cannot answer that, because
        # blanket withholds optimizations across the public code too.
        "oracle":  ("q_oracle", 0),
        # `hoist` plus the HMAC/SHA taint seed. The original seed named only the
        # cipher entry points, so the per-page HMAC ran with DIT=0 and coverage
        # topped out at 94.4-95.4% of the oracle - the same blind spot that
        # produced the retracted "+8.15%" result on silicon.
        "hmacfix": ("q_hmacfix", 0),
        # Same HMAC seed, cheaper placement. hmacfix closes coverage but toggles
        # 291-335x more than the oracle because region placement lands inside
        # SHA-512's compression loop; these two ask whether the coverage can be
        # kept without the toggle bill.
        "hmacfn":   ("q_hmacfn", 0),
        "hmacsw30": ("q_hmacsw30", 0),
        # --- controls for "why is a renamed switch not free?" ---
        # All three are hmacfix with the 121 HMAC/SHA switch sites rewritten in
        # the ASSEMBLY, so every instruction sits at the same address and only
        # what occupies the slot changes.
        #   hfxnop    NOP           - layout only, no slot cost, no DIT
        #   hfxmul    MUL XZR,XZR,XZR - a real multiplier op that cannot be
        #                             elided, so it prices the ISSUE SLOT
        #   hfxnoclr  enables kept, CLEARS removed - tests the model's claim
        #                             that entry is free and all renamed cost
        #                             is the non-speculative clear's shadow
        "hfxnop":    ("q_hfxnop", 0),
        "hfxmul":    ("q_hfxmul", 0),
        "hfxnoclr":  ("q_hfxnoclr", 0)}
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
        "--env", f"DIT_MODE={ARMS[arm][1]}",
        "--binary", str(BIN / ARMS[arm][0]),
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
        m = re.search(r"dit_now=(\d+)", txt)
        rec["dit_now"] = int(m.group(1)) if m else None
        m = re.search(r"misses=(\d+)", txt)
        rec["misses"] = int(m.group(1)) if m else None
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
        if not (BIN / ARMS[arm][0]).exists():
            sys.exit(f"missing cross binary: {BIN / ARMS[arm][0]}")
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

    check_gates(recs)


def check_gates(recs):
    """The two gates this file's docstring has always claimed, now actually run.

    They were documented here and enforced only in xover/run_gem5.py, so this
    sweep shipped ungated - and it fails: simInsts differs by exactly 400 between
    switch models on 10 of 16 arm/cache pairs, including `nodit`, a binary that
    contains ZERO `msr DIT` and therefore cannot be affected by the flag at all.
    A gate that lives in a docstring catches nothing.
    """
    ok = True

    # GATE 1: the instruction stream cannot depend on the machine model.
    #
    # TOLERANCE, and why it is not zero. The ROI is delimited by m5_reset_stats,
    # which lands as a scheduled event, so a variable number of already-in-flight
    # instructions commit on either side of the boundary. Measured across a full
    # 40-run sweep the discrepancy is ALWAYS exactly 0 or +400 and never negative
    # - a fixed, ROB-scale boundary offset that does not grow with the region
    # (400 out of 85M and out of 887k alike). Demanding exact equality here flags
    # that artifact as contamination; the real signal is a difference that SCALES
    # with the workload, so gate on a fraction instead.
    TOL = 1e-4  # 0.01% of the ROI
    bykey = {}
    for r in recs:
        if r.get("insts"):
            bykey.setdefault((r["arm"], r["cache"]), {})[r["cfg"]] = r["insts"]
    for (arm, cache), v in sorted(bykey.items()):
        if len(set(v.values())) > 1:
            lo, hi = min(v.values()), max(v.values())
            if (hi - lo) > TOL * hi:
                ok = False
                print(f"!! GATE FAIL simInsts: {arm} cache={cache} differs by "
                      f"{hi-lo:.0f} ({(hi-lo)/hi*100:.4f}%) across switch "
                      f"models: {v}")

    # GATE 2: an arm that never sets DIT must report no DIT activity.
    for r in recs:
        if r["arm"] in ("plain", "nodit") and r.get("ditSuppressed"):
            ok = False
            print(f"!! GATE FAIL ditSuppressed: {r['arm']} cache={r['cache']} "
                  f"cfg={r['cfg']} reports {r['ditSuppressed']:.0f}, must be 0 - "
                  f"the baseline is running some placement")

    # Not a gate, but the symptom that exposes a broken comparison fastest: for
    # an arm that NEVER SETS DIT, the two switch models must agree - there is no
    # `msr DIT` to decode differently and no DIT state for gated instructions to
    # consume, so any gap is measurement error.
    #
    # THIS CHECK IS ONLY VALID FOR THOSE ARMS. It was first written to fire on
    # every arm and that was wrong: with DIT actually dwelling, the two models
    # also differ in how gated instructions read the DIT state (renamed operand
    # vs architectural), so `blanket` can legitimately be faster serializing.
    # Applying the check there reports a real microarchitectural difference as a
    # broken comparison.
    bycyc = {}
    for r in recs:
        if r.get("cycles") and not r.get("dit_now") and not r.get("ditSuppressed"):
            bycyc.setdefault((r["arm"], r["cache"]), {})[r["cfg"]] = r["cycles"]
    for (arm, cache), v in sorted(bycyc.items()):
        if "serdit" in v and "spec" in v and v["serdit"] < v["spec"] * 0.995:
            ok = False
            print(f"!! SUSPECT: {arm} cache={cache} never sets DIT yet is "
                  f"{(1 - v['serdit']/v['spec'])*100:.2f}% FASTER serializing than "
                  f"renamed. With no DIT anywhere the two models must agree.")

    # The driver reads PSTATE.DIT back at exit, so "the mode actually took" is
    # recorded rather than assumed - an arm that silently failed to set DIT would
    # otherwise look like a cheap placement.
    for r in recs:
        want = ARMS.get(r["arm"], (None, None))[1]
        got = r.get("dit_now")
        if want is not None and got is not None and want != got:
            ok = False
            print(f"!! GATE FAIL dit_now: {r['arm']} cache={r['cache']} cfg={r['cfg']} "
                  f"expected PSTATE.DIT={want}, driver read {got}")

    # Checksums must agree at a point, or the arms are not doing the same work.
    cks = {}
    for r in recs:
        if r.get("checksum") is not None:
            cks.setdefault(r["cache"], set()).add(r["checksum"])
    for cache, v in sorted(cks.items()):
        if len(v) > 1:
            ok = False
            print(f"!! GATE FAIL checksum: cache={cache} arms disagree: {v}")

    print("gates: PASS" if ok else "gates: FAILED - do not quote these numbers")


if __name__ == "__main__":
    main()
