#!/usr/bin/env python3
"""Experiment 09's benchmarks under gem5, with and without switch serialisation.

THE QUESTION SILICON CANNOT ANSWER. Experiment 09 concludes that the pass's
entire cost on libsodium is switch serialisation with no dwell term: blanket
PSTATE.DIT is free (-0.60% to +1.95%), the pass costs +12% to +124%, and
cycles-per-switch comes out at 41.2 / 40.3 / 44.8 across three independent
benchmarks. But that last column is measured cycles divided by measured
switches, so multiplying it back reproduces the measurement by construction --
the README says so itself. An M5 has exactly one `msr DIT` implementation and it
serialises, so the mechanism cannot be turned off there.

gem5 can. `--no-speculative-dit` selects the serialising path; without it the
write is a renamed CC-register write. Same binary, same input, one mechanism
changed. Two predictions fall out of experiment 09 and both are falsifiable:

  1. Under the renamed model the pass overhead should collapse toward blanket's
     ~0%. If it does not, there is a dwell term experiment 09 missed.
  2. "Coarser beats finer on all six" should stop holding -- taintfn (569
     switches), taint (521) and fine (631) should converge, because the ordering
     was allegedly pure toggle count. If fine still loses under a renamed
     switch, that ordering was never about toggles.

FOUR-TERM DECOMPOSITION. The arms are chosen so the cost separates instead of
arriving as one number:

  (rt     - base)  codegen cost of the hardening pipeline itself, 0 switches
  (nop    - rt)    pure code layout of the switches, HINT #0, never executes
  (spec   - nop)   a RENAMED switch executing
  (serdit - spec)  serialising it            <- the term this rig exists for

WHY NO REPS. gem5 is deterministic, so the 15-rep median the silicon run needed
is replaced by exact gates. Host load, NUMA placement and core count cannot move
a simulated cycle count, which is why 70 runs can go out at once on this machine
without biasing anything.

ROI IS ONE CRYPTO CALL. CIO's drivers already bracket exactly one call with
START_CYCLE_TIMER / STOP_CYCLE_TIMER; our eval_util.h turns those into
m5_reset_stats / m5_dump_reset_stats, so each stats dump is precisely the call
the M5 run timed. Warmup dumps arrive first and are dropped rather than averaged
in -- the native rig could not do that, and its reg_n counted 1025 regions
against 1000 timing samples for exactly this reason.
"""
import argparse, csv, hashlib, json, os, pathlib, re, shutil, statistics, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

G5 = pathlib.Path(os.environ.get("G5", pathlib.Path.home() / "Documents/gem5-DIT"))
CONFIG = G5 / "configs/example/arm/fdp_neoverse_v2_binary.py"
WORK = pathlib.Path(os.environ.get("WORK", pathlib.Path.home() / "Documents/libsodium-cioparity"))

# The switch-model axis. Everything else about the machine is held fixed; the
# three optimization flags are the DIT-gated ones and must be ON, or there is
# nothing for the mode to suppress and every arm reads the same.
CONFIGS = {
    "spec":   ["--eves", "--dmp", "--comp-simp"],
    "serdit": ["--eves", "--dmp", "--comp-simp", "--no-speculative-dit"],
}

# base IS the MIR round-trip control on the whole-library path (same .pe.mir,
# pass never run), so it already controls for the lowering pipeline and there is
# no separate `rt` arm. The per-TU path in build_arms.sh does need one.
# Each placement policy carries its OWN layout control (<policy>nop: identical
# placement, HINT #0 in place of every switch). A single `nop` from taint.mir
# controls for taint's layout only, and without per-policy controls a
# cross-policy comparison conflates placement with the per-binary codegen
# lottery -- measured at 6.69% on aes256gcm_decrypt, enough to invert the
# ranking. Pair every policy with its own nop before quoting a ranking.
ARMS = ["base", "blanket",
        "taint", "taintnop",
        "taintfn", "taintfnnop",
        "fine", "finenop"]
NOP_OF = {"taint": "taintnop", "taintfn": "taintfnnop", "fine": "finenop"}
# Arms in which no `msr DIT` ever executes: the switch model must not move them,
# and dwell must be exactly zero.
INERT = ("base", "taintnop", "taintfnnop", "finenop", "nop")

# bench -> (iters, warmup, ad_size or None). A fixed 100-char message, not a
# random one: identical input across all 70 cells.
MSG = "".join("cioparity100byteMessageForTheLibsodiumSwitchModelStudy"[i % 54] for i in range(100))
# Warmup is 10, not CIO's 25 and not the 5 this rig started with. Measured on
# the ed25519 smoke run: per-call cycles climb 74,605 -> 78,413 over the first
# seven regions as the caches and predictors fill, then hold at 78,413 +/- 20.
# Five warmup regions therefore leave two unsettled regions inside the measured
# window; ten clears the transient with margin. gem5 is deterministic, so once
# settled a handful of regions is as good as a thousand -- there is no noise to
# average down, which is why the counts here are small.
BENCHES = {
    "ed25519":                    (20, 10, None),
    "chacha20_poly1305_encrypt":  (50, 10, 100),
    "chacha20_poly1305_decrypt":  (50, 10, 100),
    "aesni256gcm_encrypt":        (50, 10, 100),
    "aesni256gcm_decrypt":        (50, 10, 100),
    # argon2id: ONE measured op and ONE warmup. gem5 is deterministic, so a
    # settled region is exact and every extra op is another 326M cycles for
    # nothing; warmup=10 here would be ten hours per cell. Its driver's argv is
    # <niter> <nwarmup> <password> <OUT_SIZE> <ccfile>, so the third slot is the
    # output size, not AD - 32 bytes, CIO's value. Not in the default sweep: run
    # it with --benches argon2id (reproduce.sh does).
    "argon2id":                   (1, 1, 32),
}
DEFAULT_BENCHES = [b for b in BENCHES if b != "argon2id"]

STAT_RE = re.compile(r"^(\S+)\s+([-\d.]+(?:e[-+]?\d+)?|nan|inf|-inf)\s")


def all_dumps(path):
    """Every Begin/End block, in order. The native rig's analogue parses only the
    first; here each block is one measured crypto call and the sequence matters."""
    dumps, cur, inside = [], {}, False
    with open(path) as fh:
        for line in fh:
            if line.startswith("---------- Begin Simulation Statistics"):
                inside, cur = True, {}
                continue
            if line.startswith("---------- End Simulation Statistics"):
                if inside:
                    dumps.append(cur)
                inside = False
                continue
            if not inside:
                continue
            m = STAT_RE.match(line)
            if m:
                try:
                    cur[m.group(1)] = float(m.group(2))
                except ValueError:
                    pass
    return dumps


def pick(s, *keys):
    for k in keys:
        for full, v in s.items():
            if full.endswith(k):
                return v
    return None


def canon(src, *key):
    """Hard-link to a fixed-WIDTH path. gem5 SE writes the binary path onto the
    initial process stack as argv[0], so its LENGTH shifts stack alignment for
    the whole run: one byte-identical binary measured 287,318 / 285,068 / 284,936
    cycles at a 1-, 36- and 18-char name -- 0.84% from the file name alone."""
    slot = hashlib.md5("/".join(map(str, key)).encode()).hexdigest()[:8]
    # The slot fixes the LAST component's width; the prefix must be fixed too, or
    # two sweeps in differently named WORK dirs run with different argv[0]
    # lengths and are not comparable (measured: 71 vs 63 chars, 0.2-2.6% shift).
    # /tmp/<12-hex of WORK> is the same length for every WORK on every machine.
    root = pathlib.Path("/tmp") / ("cio_" + hashlib.md5(str(WORK).encode()).hexdigest()[:12])
    c = root / slot / "b"
    c.parent.mkdir(parents=True, exist_ok=True)
    if c.exists():
        c.unlink()
    try:
        os.link(src, c)
    except OSError:
        shutil.copy2(src, c)
    return c


def run_one(job):
    bench, arm, cfg, a, outroot = job
    iters, warm, ad = BENCHES[bench]
    tag = f"{bench}__{arm}__{cfg}"
    d = outroot / tag
    d.mkdir(parents=True, exist_ok=True)
    src = WORK / "bin" / f"eval_{bench}.{arm}"
    if not src.exists():
        return {"bench": bench, "arm": arm, "cfg": cfg, "error": f"missing {src}"}
    binpath = canon(src, bench, arm, cfg)

    argv = [str(iters), str(warm), MSG] + ([str(ad)] if ad is not None else []) + ["cc.txt"]
    cmd = [str(G5 / "build/ARM" / a.gem5), f"--outdir={d}", str(CONFIG),
           "--binary", str(binpath), "--arguments", " ".join(argv)] + CONFIGS[cfg]

    t0 = time.time()
    if not ((d / "stats.txt").exists() and a.resume):
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=str(d))
        (d / "run.log").write_text(p.stdout + p.stderr)
    wall = time.time() - t0

    log = (d / "run.log").read_text() if (d / "run.log").exists() else ""
    dumps = all_dumps(d / "stats.txt") if (d / "stats.txt").exists() else []
    # Take EXACTLY the measured window: dumps[warm : warm+iters]. Slicing to the
    # end instead sweeps in gem5's at-exit dump, which covers the driver writing
    # its results file after the last reset -- the smoke run read 187,764
    # insts/op that way against a true 171,800, a 9% error from one stray block.
    roi = dumps[warm:warm + iters] if len(dumps) >= warm + iters else []

    def total(*keys):
        vals = [pick(s, *keys) for s in roi]
        vals = [v for v in vals if v is not None]
        return sum(vals) if vals else None

    n = len(roi)
    cyc, ins = total("core.numCycles", "numCycles"), total("commitStats0.numInsts", "simInsts")
    r = {"bench": bench, "arm": arm, "cfg": cfg, "wall_s": round(wall, 1),
         "dumps": len(dumps), "roi_n": n,
         "cycles_total": cyc, "insts_total": ins,
         "cycles_per_op": round(cyc / n, 1) if cyc and n else None,
         "insts_per_op": round(ins / n, 1) if ins and n else None,
         "dit_writes": total("commit.ditWrites"),
         "dit_set": total("commit.ditSetImm"), "dit_clear": total("commit.ditClearImm"),
         "dit_write_reg": total("commit.ditWriteReg"), "dit_read": total("commit.ditRead"),
         "dit_suppressed": total("ditSuppressed"),
         # commit.ditCycles: cycles with the mode SET (gem5 branch `ditcycles`).
         # The dwell axis the five switch counters cannot provide. None when the
         # simulator predates it, so every consumer must tolerate that.
         "dit_cycles": total("commit.ditCycles"),
         "sim_insts_whole": None,
         "dit_exit": None,
         "failed_assert": ("FAILURE" in log) or ("Assertion" in log)}
    m = re.search(r"CIOGEM5 exit dit=(\d)", log)
    if m:
        r["dit_exit"] = int(m.group(1))
    # Whole-run instruction count, for the cross-model determinism gate. The LAST
    # dump is gem5 teardown after the final reset, so sum every dump instead.
    r["sim_insts_whole"] = sum(v for v in (pick(s, "commitStats0.numInsts", "simInsts")
                                           for s in dumps) if v is not None) or None
    if not roi:
        r["error"] = f"no ROI dumps (got {len(dumps)}, need > {warm})"
    print(f"  [{'ok ' if not r.get('error') else 'ERR'}] {tag:52s} "
          f"cyc/op={r['cycles_per_op']} sw/op="
          f"{round(r['dit_writes']/n,2) if r['dit_writes'] is not None and n else None} "
          f"({wall:.0f}s)", flush=True)
    return r


def gates(results, arms, cfgs):
    """Exact gates. gem5 is deterministic, so any of these failing is a real
    defect in the rig, not noise -- there is no 'within tolerance' here."""
    fails, notes = [], []
    by = {(r["bench"], r["arm"], r["cfg"]): r for r in results if not r.get("error")}

    for r in results:
        if r.get("error"):
            fails.append(f"{r['bench']}/{r['arm']}/{r['cfg']}: {r['error']}")
            continue
        if r["failed_assert"]:
            fails.append(f"{r['bench']}/{r['arm']}/{r['cfg']}: driver reported FAILURE "
                         "-- the crypto did not verify, so the arm is not computing the "
                         "same thing")
        # An arm that exits with DIT set leaked the mode past an unbalanced exit
        # and is blanket in disguise. That is the tail-call bug.
        want_exit = 1 if r["arm"] == "blanket" else 0
        if r["dit_exit"] is not None and r["dit_exit"] != want_exit:
            fails.append(f"{r['bench']}/{r['arm']}/{r['cfg']}: exits with dit="
                         f"{r['dit_exit']}, expected {want_exit}")
        # No mode is ever set in these three, so the machine must suppress nothing.
        if r["arm"] in INERT:
            if r["dit_writes"]:
                fails.append(f"{r['bench']}/{r['arm']}/{r['cfg']}: {r['dit_writes']} DIT "
                             "writes committed in an arm that must have none")
            if r["dit_suppressed"]:
                fails.append(f"{r['bench']}/{r['arm']}/{r['cfg']}: ditSuppressed="
                             f"{r['dit_suppressed']} with the mode never set")
        # Blanket sets the mode once in a constructor, before any ROI, so it
        # must toggle nothing inside the region.
        if r["arm"] == "blanket":
            if r["dit_writes"]:
                fails.append(f"{r['bench']}/blanket/{r['cfg']}: {r['dit_writes']} DIT writes "
                             "inside the ROI -- blanket must toggle nothing")
        # The pass arms must TOGGLE, or placement inserted nothing that executes.
        if r["arm"] in NOP_OF:
            if not r["dit_writes"]:
                fails.append(f"{r['bench']}/{r['arm']}/{r['cfg']}: 0 committed DIT writes -- "
                             "placement inserted nothing that executes")
        # An inert arm must spend ZERO cycles with the mode set. This is the
        # direct check that <policy>nop really is inert, rather than inferring
        # it from the switch count.
        if r["arm"] in INERT and r.get("dit_cycles"):
            fails.append(f"{r['bench']}/{r['arm']}/{r['cfg']}: ditCycles="
                         f"{r['dit_cycles']} in an arm where no msr DIT executes")
        # Blanket must dwell for essentially the whole region, or it is not
        # blanket. Uses ditCycles rather than ditSuppressed, which is only
        # evidence when a DIT-gated optimisation happened to be eligible.
        if r["arm"] == "blanket" and r.get("dit_cycles") and r["cycles_total"]:
            frac = r["dit_cycles"] / r["cycles_total"]
            if frac < 0.95:
                fails.append(f"{r['bench']}/blanket/{r['cfg']}: only {frac:.1%} of the "
                             "region ran with the mode set")

        # NOT A GATE: ditSuppressed > 0. It was one, on `blanket` and on all
        # three pass arms, and it was WRONG -- it failed 16 of 24 AES-GCM cells
        # as false alarms. ditSuppressed counts DIT-GATED OPTIMISATIONS that the
        # mode actually suppressed, so it is evidence the mode is active only on
        # code where such an optimisation was eligible in the first place. AES
        # and PMULL offer comp-simp nothing, so AES-GCM reads ditSuppressed=0 in
        # every arm while `fine` commits 900 DIT writes and the mode is provably
        # set. Sufficient, not necessary -- and as a gate it would block the
        # experiment on any workload whose hot path is not comp-simp-eligible.
        #
        # The sound witnesses that the mode is genuinely on are (a) committed
        # `msr DIT` writes, gated above, and (b) the mode readback, which lives
        # on the separate -DDIT_READBACK binaries built by
        # `build_arms_wl.sh gate` (it cannot live here: a `mrs DIT` anywhere in
        # a timing binary decodes differently under the two switch models and
        # perturbs the measurement -- see blanket_ctor.c).
        if r["arm"] in ("taint", "taintfn", "fine", "blanket") and \
                not r["dit_suppressed"]:
            notes.append(f"{r['bench']}/{r['arm']}/{r['cfg']}: ditSuppressed=0 -- the mode is "
                         "on (see committed DIT writes / the gate-stage readback) but no "
                         "DIT-gated optimisation was eligible on this code")

    # One binary, one input, two machine configs: the instruction stream must be
    # identical. If it is not, the driver is perturbing itself -- the simulated
    # -time trap. This is the gate that catches a self-timing driver.
    for bench in BENCHES:
        for arm in arms:
            vals = {c: by[(bench, arm, c)]["sim_insts_whole"]
                    for c in cfgs if (bench, arm, c) in by}
            if len(set(v for v in vals.values() if v is not None)) > 1:
                fails.append(f"{bench}/{arm}: simInsts differs across switch models "
                             f"{vals} -- the run depends on its own timing")
    # An arm in which no `msr DIT` ever executes must be UNAFFECTED by the switch
    # model -- identical cycles, not merely close. gem5 is deterministic, so any
    # difference here means --no-speculative-dit is perturbing something other
    # than the DIT write, and every serialisation number in the run would carry
    # that as an unattributed offset. This is the control for the axis itself.
    for bench in BENCHES:
        for arm in INERT:
            vals = {c: by[(bench, arm, c)]["cycles_total"]
                    for c in cfgs if (bench, arm, c) in by}
            if len(set(v for v in vals.values() if v is not None)) > 1:
                fails.append(f"{bench}/{arm}: cycles differ across switch models {vals} "
                             "-- no DIT executes in this arm, so the model must not "
                             "change it; the serialisation term is confounded")

    # Same placement, same instruction count: nop must match taint. This is what
    # makes (taint - nop) attributable to the switch rather than to code layout.
    for bench in BENCHES:
        for c in cfgs:
            for pol, nop in NOP_OF.items():
                a, b = by.get((bench, nop, c)), by.get((bench, pol, c))
                if a and b and a["insts_per_op"] and b["insts_per_op"]:
                    d = abs(a["insts_per_op"] - b["insts_per_op"]) / b["insts_per_op"]
                    if d > 0.005:
                        fails.append(f"{bench}/{c}: {nop} vs {pol} insts/op differ by "
                                     f"{d:.2%} -- HINT #0 substitution changed the "
                                     f"instruction count, so ({pol} - {nop}) is not a "
                                     "pure switch term")
    return fails, notes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--benches", default=",".join(DEFAULT_BENCHES))
    ap.add_argument("--arms", default=",".join(ARMS))
    ap.add_argument("--configs", default="spec,serdit")
    ap.add_argument("--jobs", type=int, default=100)
    ap.add_argument("--gem5", default="gem5.opt")
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--out", default=str(WORK / "out"))
    a = ap.parse_args()

    benches = [b for b in a.benches.split(",") if b]
    arms, cfgs = a.arms.split(","), a.configs.split(",")
    outroot = pathlib.Path(a.out); outroot.mkdir(parents=True, exist_ok=True)
    jobs = [(b, arm, c, a, outroot) for b in benches for arm in arms for c in cfgs]
    print(f"{len(jobs)} runs, {a.jobs} at a time, {a.gem5}\n", flush=True)

    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        results = list(ex.map(run_one, jobs))

    with open(outroot / "results.jsonl", "w") as fh:
        for r in results:
            fh.write(json.dumps(r) + "\n")
    cols = ["bench", "arm", "cfg", "roi_n", "cycles_per_op", "insts_per_op",
            "dit_writes", "dit_set", "dit_clear", "dit_read", "dit_suppressed",
            "dit_cycles", "dit_exit", "wall_s"]
    with open(outroot / "results.csv", "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for r in sorted(results, key=lambda r: (r["bench"], r["arm"], r["cfg"])):
            w.writerow(r)
    print(f"\nwrote {outroot}/results.{{jsonl,csv}}")

    print("\n=== gates ===")
    fails, notes = gates(results, arms, cfgs)
    for n in notes:
        print(f"  note {n}")
    for f in fails:
        print(f"  FAIL {f}")
    print("  all gates pass" if not fails else f"  {len(fails)} gate failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
