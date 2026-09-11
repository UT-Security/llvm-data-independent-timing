#!/usr/bin/env python3
"""Experiment 02 on real silicon: the secret-fraction crossover on an Apple M4.

The gem5 half of experiment 02 (paper_experiments/02-libsodium-signed-lookup)
runs this exact driver on a Neoverse-V2 with EVES and VTAGE and finds a
crossover: blanket DIT's cost is the load-value predictions the mode switches
off, so it grows with the public lane, while selective placement pays a fixed
per-request switch bill. This runs the same source on hardware.

WHAT IS THE SAME: the driver (byte-identical, sha256 checked by
build_silicon.sh), the library arms and their seeds (utils/taint_libsodium_arms.sh,
which is kept in sync with the gem5 rig's build_arms.sh), the request shape, the
L sweep, and the way f_secret is measured (--nosecret, never assumed).

WHAT IS DIFFERENT, and it is not a detail:

  * The instrument. PMC0/PMC1 read from EL0 (see pmc_ipc.h), not kperf and not
    gem5 statistics. Exact instruction counts, one `mrs` per boundary.
  * `msr DIT` is SERIALISING here, because that is what the hardware does. The
    gem5 figure's "renamed" curve is the counterfactual only a simulator can
    run; on silicon there is one pass curve and it is the serialised one.
    Measured on this M4: 34.0 cycles per write (utils/... probe, and the
    cycles-per-switch column below re-derives it from the arms).
  * THE LANE. The public lane reads a record header on q of its iterations --
    a constant value at a data-dependent address, the load a value predictor is
    supposed to remove from the critical path. On the canonical lane that
    constant is 0x2545F4914F6CDD1D, 62 bits wide, and the M4 CANNOT PREDICT IT:
    blanket DIT reads +0.0% at every q from 0 to 1, cycles within 0.03% and IPC
    identical to three decimals, while gem5 reads +31%. The M4's predictor
    holds a bounded number of value bits, and the boundary is exactly 36
    (measured on this driver: 36 bits predicted, 37 not, nothing in between).
    Give the same lane a header that fits -- `--lane narrow`, HDR_CONST =
    0xCAFEBABE, one constant and no code changed -- and the M4 reads
    +12.2/+23.7/+34.5/+45.5% at q = .25/.5/.75/1, against gem5's own
    +11.4/+25.1/+30.9/+40.4%. So both lanes are run: `wide` is the negative
    result and `narrow` is the crossover. `--hdr-sweep` measures the boundary.

THE GATES, all fatal except where noted.
  1. dit readback   - blanket must exit with PSTATE.DIT set, every other arm clear.
  2. pub_dit        - the fraction of requests entering the PUBLIC lane with DIT
                      already set. 1.0 for blanket, 0.0 for everything else. This
                      is the gate that catches a tail-call leak turning selective
                      placement into blanket in disguise; it read 1.000 once.
  3. checksum       - every arm must compute the same PUBLIC-lane checksum at
                      the same L. It has to be the public lane: libsodium draws
                      a fresh AEAD key per process, so the ciphertext byte the
                      full run folds into `sink` differs between two runs of the
                      SAME arm and cannot gate anything. The --nosecret pass is
                      run for every arm, not just the baseline, which makes the
                      gate exact and gives the public-lane penalty C_public per
                      arm for free -- the quantity the crossover is made of.
  3b. instructions  - nodit and blanket are one binary and must retire the same
                      count (+/- a tolerance: Apple's fixed PMC1 counts kernel
                      entries too, measured at ~0.01% run to run). The pass arm
                      must retire MORE, by its executed switches; that delta,
                      divided by 2, is the executed switch count per request and
                      is reported.
  4. implied clock  - cycles / CNTVCT seconds must land on a P-core (3.4-5.0 GHz).
                      The PMCs are per-core and this run cannot pin without root,
                      so a thread that migrates mid-ROI differences two cores'
                      counters. Such a sample is REJECTED AND COUNTED, never
                      silently dropped, and the reject rate is printed with the
                      numbers. A cell whose reject rate is high is not a cell
                      whose numbers you may quote.

ROOT is not required and buys one thing: kern.sched_thread_bind_cpu, which pins
the thread and makes gate 4 unable to fire. Run under sudo if you have it.

  python3 run_crossover_m4.py                       # the crossover sweep
  python3 run_crossover_m4.py --tblbits-sweep       # where DIT starts costing
  python3 run_crossover_m4.py --q-sweep             # cost vs the predictable fraction
"""
import argparse
import json
import os
import platform
import re
import statistics
import subprocess
import sys
import time

D = os.path.dirname(os.path.abspath(__file__))
# Where the arm binaries are. `--bin` retargets it, which is how a build with a
# different secret lane (build_silicon.sh's SECRET_OP=aes, into its own OUT) is
# swept without disturbing the committed chacha binaries: the arms, the gates
# and the argv equalisation are identical, only the AEAD underneath differs.
BIN = os.path.join(D, "bin")

# arm -> (library/link variant, extra argv). nodit and blanket are ONE binary:
# the blanket arm is the same codegen with the mode set before the ROI, so no
# instruction inside the measured region differs between them.
ARMS = {
    "nodit":      ("base",       []),
    "blanket":    ("base",       ["--blanket"]),
    # Apple's own bracket around the AEAD entry points: read the previous DIT
    # state, `msr DIT, #1`, `sb`, the call, and clear only if it was clear. Two
    # mode writes and one barrier per CALL, against the pass's ~40 writes per
    # request. This is what AWS-LC ships (experiment 14's `ditsb`) and what
    # Apple's own guidance tells a library author to write. The unhardened
    # library; only the wrapper is added.
    "bracket":    ("bracket",    []),
    # Its instruction-matched twin: the same 17 instructions at the same
    # addresses, the token read a `mov xzr` and both writes and the barrier
    # `nop`, so no mode ever changes. (bracket - bracketnop) is the bracket's
    # real cost. Experiment 09 ran this arm without a twin and had to borrow a
    # barrier arm for the column; that is the mistake this exists to not repeat.
    "bracketnop": ("bracketnop", []),
    # Apple's sequence with the speculation barrier removed. NOT a shippable
    # configuration -- the barrier is what makes the mode change apply to what
    # follows -- but (bracket - bracketnobar) is what `sb` costs, and on this
    # part in this flow that turns out to be most of the bracket.
    "bracketnobar": ("bracketnobar", []),
    # Apple's documented fallback barrier for a part without FEAT_SB,
    # `dsb nsh; isb sy`. This part HAS FEAT_SB and Apple's own
    # libsystem_platform.dylib uses `sb` on it, so this is not the shipping
    # sequence here -- it is the one gem5 could model, having dsb and isb and no
    # sb.
    "bracketdsb": ("bracketdsb", []),
    "bracketdsbnop": ("bracketdsbnop", []),
    "pass":       ("taint",      []),
    "nop":        ("taintnop",   []),
}
ARM_ORDER = ["nodit", "blanket", "bracket", "bracketnop", "bracketnobar",
             "bracketdsb", "bracketdsbnop", "pass", "nop"]
# arm -> its instruction-matched layout control. (arm - twin) is what the mode
# writes cost; everything else the arm did to the binary is in the twin too.
# Each arm against a twin with ITS OWN instruction count. bracketdsb cannot use
# bracketnop: `dsb nsh; isb sy` is two instructions where `sb` is one.
TWIN = {"pass": "nop", "bracket": "bracketnop", "bracketnobar": "bracketnop",
        "bracketdsb": "bracketdsbnop"}
LANES = ("wide", "narrow")
# The header-width sweep. Each entry is its own BINARY, because HDR_CONST is a
# compile-time -D: lane "bN" is built with a header of N one-bits, so the only
# thing that changes across the sweep is how many value bits the predictor is
# asked to hold. build_silicon.sh knows the same naming.
HDR = {"wide": ("0x2545F4914F6CDD1D", 62), "narrow": ("0xCAFEBABE", 32)}
for _n in (8, 16, 24, 32, 34, 35, 36, 37, 38, 40, 48, 56, 62):
    HDR[f"b{_n}"] = (hex((1 << _n) - 1), _n)

# EVERY ARM MUST SEE BYTE-IDENTICAL argv, and this is not fussiness.
#
# The request's retired instruction count moves by +/-5 (0.1%) with the stack
# alignment the process happens to get, because the AEAD's stack buffers land
# differently and macOS's memcpy dispatches on alignment -- same bytes, a
# different number of instructions. argv is what sets that alignment. Two ways
# it differed between arms before this existed:
#
#   * `--blanket`, 9 characters, which only the blanket arm passes. Measured:
#     nodit read 5,137.2 instructions per request and blanket 5,147.9, a
#     systematic +10 that has nothing to do with the mode -- and padding nodit's
#     argv to the same length reproduced blanket's number exactly (5,147.3).
#   * argv[0]. The arms' binaries are native_base_narrow (18 characters) through
#     native_bracketnobar_narrow (26), so every arm sat at a different alignment.
#
# Both are fixed here: the binaries are reached through a farm of equal-length
# symlinks, and the arms that do not pass `--blanket` pass an ignored filler of
# the same length instead. The driver's argument loop ignores what it does not
# recognise, so the filler costs one strcmp before the ROI opens.
#
# This is the instruction-count version of the argv[0] sensitivity the gem5 rig
# already controls for by rooting its binaries at a constant-length /tmp path
# (paper_experiments/02, "Validity gates").
BLANKET_ARG = "--blanket"
FILLER_ARG = "--padding"          # same length, and the driver ignores it
assert len(FILLER_ARG) == len(BLANKET_ARG)

LINE = re.compile(r"^signed_lookup ")
PMC = re.compile(r"^PMC exit ")


class Reject(Exception):
    pass


def parse(out, err):
    line = next((l for l in out.splitlines() if LINE.match(l)), None)
    pline = next((l for l in err.splitlines() if PMC.match(l)), None)
    if line is None:
        raise RuntimeError("driver printed no result line:\n" + out + err)
    if pline is None:
        raise RuntimeError("no PMC exit line -- is this the PMC build?\n" + err)
    f = dict(kv.split("=", 1) for kv in line.split()[1:])
    p = dict(kv.split("=", 1) for kv in pline.split()[2:])
    return f, p


_FARM = {}


def farm_path(v, lane):
    """An equal-length path to this arm's binary. See the argv note above.

    The symlinks live next to the binaries and are named by a zero-padded index,
    so every arm's argv[0] is the same number of bytes. Created on demand and
    reused; a stale link is replaced rather than trusted.
    """
    key = (v, lane)
    if key in _FARM:
        return _FARM[key]
    target = os.path.join(BIN, f"native_{v}_{lane}")
    d = os.path.join(BIN, "argv")
    os.makedirs(d, exist_ok=True)
    # the index is the arm's position in a fixed list, so a given arm always
    # gets the same name and two runs of the rig are comparable
    order = sorted({a[0] for a in ARMS.values()} | set(LANES) | {v, lane})
    idx = sorted({a[0] for a in ARMS.values()} | {v}).index(v)
    ln = sorted(set(LANES) | {lane}).index(lane)
    link = os.path.join(d, f"a{idx:02d}{ln:02d}")
    try:
        if os.path.islink(link) or os.path.exists(link):
            os.unlink(link)
        os.symlink(os.path.abspath(target), link)
    except OSError as e:
        sys.exit(f"FATAL: could not create the equal-length argv[0] link {link}: {e}")
    _FARM[key] = link
    return link


def one(arm, args, lane="narrow", timeout=600):
    """One process. Returns (fields, pmcfields) or raises Reject/SystemExit."""
    v, extra = ARMS[arm]
    path = farm_path(v, lane)
    if not os.path.exists(path):
        sys.exit(f"no binary behind {path} -- run build_silicon.sh")
    # Every arm passes exactly one 9-character token in this slot, so argv is
    # byte-identical across arms and the stack alignment is too.
    slot = extra if extra else [FILLER_ARG]
    r = subprocess.run([path] + slot + list(args), capture_output=True,
                       text=True, timeout=timeout)
    f, p = parse(r.stdout, r.stderr)

    # ---- gate 1: the arm ran in the mode it claims
    want_dit = 1 if arm == "blanket" else 0   # the bracket RESTORES, so 0
    if int(f["dit"]) != want_dit:
        sys.exit(f"FATAL: {arm} exited with PSTATE.DIT={f['dit']}, expected {want_dit}\n  {f}")
    # ---- gate 2: pub_dit. See the module docstring; this one is why the rig exists.
    want_pub = 1.0 if arm == "blanket" else 0.0
    got_pub = float(f["pub_dit"])
    if abs(got_pub - want_pub) > 0.001:
        sys.exit(
            f"FATAL: {arm} entered the PUBLIC lane with DIT set on {got_pub:.1%} of "
            f"requests, expected {want_pub:.0%}.\n"
            "  A leaked DIT makes this arm blanket in disguise -- the whole public\n"
            "  lane runs protected and every number looks plausible. Check the\n"
            "  library for a tail call out of a hardened entry point\n"
            "  (-taint-info-loss-report, a `leak-tailcall` SEVERE record).\n"
            f"  {f}")
    if p["pmc"] != "1":
        sys.exit("FATAL: the PMCs did not read back. This host needs the "
                 "PMCR0_USEREN_EN kernel patch; without it there is no cycle "
                 "counter here that is worth the name.")
    # ---- gate 4: implied clock. Rejected and counted, not fatal.
    if p["bad"] != "0":
        raise Reject(f"implied clock {p['ghz']} GHz (cyc={p['cyc']} ncyc={p['ncyc']})")
    return f, p


def med(arm, args, reps, tries_per_rep, log, tag, lane="narrow", gap=1.03):
    """`reps` valid samples, rejecting migrated ones and counting them.

    Never silently drops: `rejects` travels with the median all the way into the
    CSV, and a cell that needed many retries is a cell to distrust.

    THE PAGE-MAPPING LOTTERY, and why the headline is the low cluster's median.
    Once the lane's tables leave L1 (128 KB of table upward on this part) a run's
    cycles per lookup come out BIMODAL: two tight states about 15% apart, each
    reproducible to 0.05% within itself, and which one a process gets is decided
    before it runs. It is not DVFS (the implied clock reads 4.40 GHz in both), it
    is not the arm (nodit and blanket split 11/10 and 12/9 over 21 runs each, and
    their two states agree to 0.05% pairwise), and it is not thermal (the states
    interleave run to run). It is the physical pages the kernel happens to hand a
    1 MB BSS, and this rig cannot choose them.
    
    The important part is that it is 15%, which is the size of the effect being
    measured -- one arm landing in the slow state is exactly what a DIT cost
    looks like. Taking a plain median over an even split is a coin toss. So the
    samples are CLUSTERED at the largest relative gap, and the reported number is
    the median of the fast cluster: the lottery only ever adds cost, so the fast
    state is the machine's own floor and it is the state both arms must be
    compared in. Every sample is still in the JSONL, and n_lo/n_hi/med_hi travel
    into the result so a cell that was measured on a split is visible as one.
    Below 128 KB of table -- which is where the canonical lane runs -- there is
    no split at all and this reduces to the median.
    """
    cyc, ins, rejects, attempts = [], [], 0, 0
    while len(cyc) < reps and attempts < reps * tries_per_rep:
        attempts += 1
        try:
            f, p = one(arm, args, lane)
        except Reject as e:
            rejects += 1
            log.write(json.dumps({"tag": tag, "arm": arm, "lane": lane,
                                  "reject": str(e)}) + "\n")
            continue
        cyc.append(float(f["cycles"]))
        ins.append(float(f["insts"]))
        log.write(json.dumps({"tag": tag, "arm": arm, "lane": lane,
                              "args": list(args), **f, "pmc": p}) + "\n")
    if len(cyc) < reps:
        sys.exit(f"FATAL: {arm} {tag}: only {len(cyc)}/{reps} samples survived the "
                 f"clock gate in {attempts} attempts ({rejects} rejects). The thread "
                 f"is migrating faster than the rig can retry; run under sudo so it "
                 f"can pin, or shorten the region.")
    pairs = sorted(zip(cyc, ins))
    ratios = [(pairs[k + 1][0] / pairs[k][0], k) for k in range(len(pairs) - 1)]
    g, cut = max(ratios) if ratios else (1.0, len(pairs) - 1)
    if g > gap:
        lo, hi = pairs[:cut + 1], pairs[cut + 1:]
    else:
        lo, hi, g = pairs, [], 1.0
    c = statistics.median(x for x, _ in lo)
    i = statistics.median(y for _, y in lo)
    spread = (lo[-1][0] - lo[0][0]) / c * 100 if c else float("nan")
    return {"cycles": c, "insts": i, "ipc": i / c, "checksum": pairs[0] and f["sink"],
            "rejects": rejects, "attempts": attempts, "spread_pct": spread,
            "n": len(lo), "n_all": len(pairs), "n_hi": len(hi), "gap": round(g, 4),
            "med_hi": round(statistics.median(x for x, _ in hi), 1) if hi else None}


def common(a, L, iters, warmup):
    return ["--tblbits", str(a.tblbits), "--lookups", str(L),
            "--iter", str(iters), "--warmup", str(warmup),
            "--predictable", str(a.q), "--chunks", str(a.chunks)]


def budget(a, L):
    """Requests to run, and warmup requests ahead of them.

    Two separate jobs. The ROI wants to be long enough that its own start and
    end noise is small and short enough that an unpinned thread is unlikely to
    move: ~40 ms. The WARMUP is not about caches here, it is about DVFS -- a
    fresh process starts at a low clock and this M4 takes tens of milliseconds
    to reach 4.4 GHz. Measured: warmup 50 (the gem5 default) reads 2.2-3.1 GHz
    and fails the clock gate on every run; ~30 ms of warmup reads 4.40-4.41 and
    passes on all of them. So the warmup is sized in TIME, not in requests.

    CALIBRATED ON THE SLOWEST ARM, not on the baseline, and that is not a
    detail. Every arm has to run the SAME iteration count or the public-lane
    checksum gate cannot compare them, so one number sets the ROI for all of
    them -- and if that number comes from the unhardened arm, the pass runs
    2.5x longer than the target, and at 25 bracketed calls per request 15x
    longer. Measured: with the baseline setting the budget, one 80 ms `pass`
    cell had 120 consecutive samples rejected as migrations and killed the
    sweep. Sizing on the slowest arm makes the target a ceiling for everyone;
    the fast arms just get a shorter ROI than they could have used.
    """
    cpr = a.cyc_per_request.get(L)
    if cpr is None:
        cpr = calibrate(a, L)
        a.cyc_per_request[L] = cpr
    ghz = 4.4e9
    iters = max(20, int(a.roi_ms * 1e-3 * ghz / cpr))
    warm = max(20, int(a.warm_ms * 1e-3 * ghz / cpr))
    return iters, warm


def calibrate(a, L):
    """Cycles per request at this L, on the SLOWEST arm this sweep will run.

    Cheap: a 40-request probe per arm. `pass` is the slowest wherever it is
    built, `bracket` next; falling back to `nodit` alone if neither is present
    is the old behaviour and is only right for a sweep that runs neither.
    """
    probe = ["--tblbits", str(a.tblbits), "--lookups", str(L), "--iter", "40",
             "--warmup", "40", "--predictable", str(a.q), "--chunks", str(a.chunks)]
    worst = 0.0
    for arm in ("nodit", "pass", "bracket"):
        if not os.path.exists(os.path.join(BIN, f"native_{ARMS[arm][0]}_{a.lane}")):
            continue
        for _ in range(6):
            try:
                f, _p = one(arm, probe, a.lane)
            except Reject:
                continue
            worst = max(worst, float(f["cycles"]) / 40.0)
            break
    if worst:
        return worst
    sys.exit(f"FATAL: could not calibrate L={L} -- every probe run migrated.")


def switch_cost():
    """Cycles per serialising `msr DIT` on this machine, from the probe.

    Measured, never assumed: it is the divisor that turns an arm's extra cycles
    into an executed switch count, and it is a property of the part.
    """
    exe = os.path.join(BIN, "dit_switch_cost")
    if not os.path.exists(exe):
        return None
    try:
        out = subprocess.run([exe], capture_output=True, text=True, timeout=300).stdout
        f = dict(kv.split("=", 1) for kv in out.split() if "=" in kv)
        return {k: float(v) for k, v in f.items()}
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return None


def provenance(a):
    import hashlib
    def sha(p):
        try:
            return hashlib.sha256(open(p, "rb").read()).hexdigest()[:16]
        except OSError:
            return "?"
    repo = os.path.abspath(os.path.join(D, "..", "..", "..", ".."))
    def git(*args):
        try:
            return subprocess.run(["git", "-C", repo] + list(args),
                                  capture_output=True, text=True).stdout.strip()
        except OSError:
            return "?"
    cpu = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                         capture_output=True, text=True).stdout.strip()
    return {
        "date": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "host": platform.node(), "cpu": cpu, "kernel": platform.release(),
        "root": os.geteuid() == 0,
        "llvm_commit": git("rev-parse", "HEAD")[:12],
        "llvm_dirty": bool(git("status", "--porcelain")),
        "driver_sha256_16": sha(os.path.join(D, "signed_lookup_gem5.c")),
        "binaries": {f"{a_}.{ln}": sha(os.path.join(BIN, f"native_{ARMS[a_][0]}_{ln}"))
                     for a_ in ARM_ORDER for ln in LANES},
        "hdr_const": {"wide": "0x2545F4914F6CDD1D (62 bits)",
                      "narrow": "0xCAFEBABE (32 bits)"},
        "lane": a.lane, "tblbits": a.tblbits, "pred_q4": a.q, "reps": a.reps,
        "chunks": a.chunks,
        "switch_cost": a.swcost,
        "roi_ms": a.roi_ms, "warm_ms": a.warm_ms,
    }


def sweep_crossover(a, log):
    """The figure. One row per (L, arm); f_secret measured per L via --nosecret.

    Every arm is run twice at every L: the full request, and the public lane
    alone. The second is not overhead -- it is the experiment's other half.
    `f_secret` needs the unhardened public-only number, the checksum gate needs
    a deterministic one (see the docstring), and `pub_pct` per arm is C_public,
    what the mode costs the lane that holds no secret. Blanket's whole bill in
    the full-flow column is that number scaled by the public lane's share.
    """
    rows = []
    print(f"== Experiment 02 on silicon: secret-fraction crossover ==")
    print(f"   {a.cpu}  lane={a.lane} (HDR_CONST "
          f"{'0x2545F4914F6CDD1D, 62 bits' if a.lane == 'wide' else '0xCAFEBABE, 32 bits'})  "
          f"tblbits={a.tblbits}  q={a.q / 4:.2f}  {a.reps} reps/cell  ROI~{a.roi_ms} ms")
    cw = a.swcost and a.swcost.get("cyc_per_write")
    print(f"   full-flow IPC overhead vs unhardened; (pub) is the public lane alone; "
          f"'brk cyc' = (bracket - its twin) cycles/request, '-no sb' the same "
          f"without the speculation barrier; "
          f"sw/req = (pass - nop) cycles / {cw:.2f} cyc per serialising msr DIT"
          if cw else "   full-flow IPC overhead vs unhardened; (pub) is the public lane alone")
    # Only the arms this sweep ran get a column. The label per arm is fixed so
    # a restricted run's table is a subset of the full run's, not a new format.
    LABEL = {"blanket": "blanket", "bracket": "Apple bracket",
             "bracketnop": "brk twin", "bracketnobar": "brk no sb",
             "bracketdsb": "brk dsb+isb", "bracketdsbnop": "dsb twin",
             "pass": "pass", "nop": "pass twin", "nodit": "nodit"}
    cols = [x for x in ARM_ORDER if x != "nodit"]
    hdr = "".join(f"{LABEL[x]:>17}" for x in cols)
    hdr += "".join(f"{'b-vs-blk':>9}" for x in ["bracket"] if x in ARM_ORDER
                   and "blanket" in ARM_ORDER)
    hdr += "".join(f"{'p-vs-blk':>9}" for x in ["pass"] if x in ARM_ORDER
                   and "blanket" in ARM_ORDER)
    hdr += "".join(f"{'brk cyc':>8}" for x in ["bracket"] if x in TWIN)
    hdr += "".join(f"{'-no sb':>8}" for x in ["bracketnobar"] if x in TWIN)
    hdr += "".join(f"{'sw/req':>7}" for x in ["pass"] if x in TWIN)
    print(f"   {'L':>6} {'req':>6} {'f_sec':>6} {'c/req':>8} {'IPC':>6} "
          + hdr + f"{'rej':>4}")
    print("   " + "-" * (48 + len(hdr) + 4))
    for L in a.L:
        iters, warm = budget(a, L)
        args = common(a, L, iters, warm)
        # Rotate which arm goes first as L advances: a fixed order lets thermal
        # drift across a long sweep look like an effect of the arm that always
        # runs last.
        k = a.L.index(L) % len(ARM_ORDER)
        order = ARM_ORDER[k:] + ARM_ORDER[:k]
        full, pub = {}, {}
        for arm in order:
            pub[arm] = med(arm, args + ["--nosecret"], a.reps, a.tries, log, f"L{L}.pub.{arm}", a.lane)
            full[arm] = med(arm, args, a.reps, a.tries, log, f"L{L}.{arm}", a.lane)
        base, basepub = full["nodit"], pub["nodit"]
        f_secret = (base["cycles"] - basepub["cycles"]) / base["cycles"] * 100

        # ---- gate 3: the public lane is the same computation in every arm
        sums = {arm: pub[arm]["checksum"] for arm in ARM_ORDER}
        if len(set(sums.values())) != 1:
            sys.exit(f"FATAL: arms disagree on the public-lane checksum at L={L}: {sums}\n"
                     "  They are not computing the same thing; no cycle ratio means anything.")
        # ---- gate 3b: nodit and blanket are one binary
        di = (abs(full["blanket"]["insts"] - base["insts"]) / base["insts"] * 100
              if "blanket" in ARM_ORDER else 0.0)
        if di > a.ins_tol:
            sys.exit(f"FATAL: blanket retired {di:.3f}% more instructions than nodit at "
                     f"L={L} (tolerance {a.ins_tol}%). They are the same binary in two "
                     "modes; a real difference means the arms are not what they claim.")
        if "bracket" in ARM_ORDER and full["bracket"]["insts"] <= base["insts"]:
            sys.exit(f"FATAL: the bracket arm retired no more instructions than nodit at "
                     f"L={L}. Apple's sequence is six instructions per call; if none "
                     "executed, the driver's calls were not renamed onto the wrapper and "
                     "this arm is the baseline under another name.")
        if "pass" in ARM_ORDER and full["pass"]["insts"] <= base["insts"]:
            sys.exit(f"FATAL: the pass arm retired no more instructions than nodit at L={L}. "
                     "Selective placement adds mode writes; if none executed, the arm is "
                     "the baseline under another name.")
        # What the mode writes cost, per arm that has a twin. NOT from the
        # instruction count: the pass retires ~73 more instructions per request
        # than unhardened but only some are `msr DIT`, and its twin retires the
        # rest too. The writes are exactly what an arm has and its twin does not,
        # so the cost is (arm - twin) cycles; dividing by the measured cost of
        # one serialising write turns it into a count. The pass lands on ~40 per
        # request against the 32 committed writes gem5 counts with
        # commit.ditWrites, and the bracket on the 2 its source says it executes.
        cw = (a.swcost or {}).get("cyc_per_write")
        dit_cyc = {k: (full[k]["cycles"] - full[t]["cycles"]) / iters
                   for k, t in TWIN.items()}
        sw_req = {k: (v / cw if cw else float("nan")) for k, v in dit_cyc.items()}

        rej = sum(full[x]["rejects"] + pub[x]["rejects"] for x in ARM_ORDER)
        for arm in ARM_ORDER:
            r, q = full[arm], pub[arm]
            rows.append({
                "L": L, "requests": iters, "warmup": warm, "tblbits": a.tblbits,
                "lane": a.lane, "pred_q4": a.q, "chunks": a.chunks,
                "f_secret_pct": round(f_secret, 2), "arm": arm,
                "cycles": r["cycles"], "insts": r["insts"], "ipc": round(r["ipc"], 4),
                "cyc_per_request": round(r["cycles"] / iters, 1),
                "vs_base_pct": round((r["cycles"] - base["cycles"]) / base["cycles"] * 100, 2),
                # The figure's y: IPC overhead, exactly as the gem5 figure computes it.
                "ipc_ovh_pct": round((base["ipc"] / r["ipc"] - 1) * 100, 2),
                "ins_vs_base_per_req": round((r["insts"] - base["insts"]) / iters, 2),
                "twin": TWIN.get(arm),
                "dit_cyc_per_req": round(dit_cyc[arm], 1) if arm in TWIN else None,
                "switches_per_req": round(sw_req[arm], 1) if arm in TWIN else None,
                "pub_cycles": q["cycles"], "pub_insts": q["insts"],
                "pub_ipc": round(q["ipc"], 4),
                "pub_vs_base_pct": round((q["cycles"] - basepub["cycles"]) / basepub["cycles"] * 100, 2),
                "pub_cyc_per_lookup": round(q["cycles"] / iters / L, 3),
                "n": r["n"], "n_all": r["n_all"], "n_hi": r["n_hi"],
                "lottery_gap": r["gap"], "cycles_slow_mode": r["med_hi"],
                "rejects": r["rejects"] + q["rejects"],
                "spread_pct": round(max(r["spread_pct"], q["spread_pct"]), 2),
                "checksum_pub": q["checksum"],
            })
        vs_blanket = lambda arm: ((full[arm]["cycles"] - full["blanket"]["cycles"])
                                  / full["blanket"]["cycles"] * 100)
        mine = {r["arm"]: r for r in rows[-len(ARM_ORDER):]}
        cell = lambda arm: (f"{mine[arm]['ipc_ovh_pct']:+7.2f}%"
                            f" ({mine[arm]['pub_vs_base_pct']:+6.2f}%)")
        line = "".join(f"{cell(x):>17}" for x in ARM_ORDER if x != "nodit")
        if "blanket" in ARM_ORDER:
            for x in ("bracket", "pass"):
                if x in ARM_ORDER:
                    line += f"{vs_blanket(x):>+8.2f}%"
        for x in ("bracket", "bracketnobar"):
            if x in TWIN:
                line += f"{dit_cyc[x]:>8.0f}"
        if "pass" in TWIN:
            line += f"{sw_req['pass']:>7.1f}"
        print(f"   {L:>6} {iters:>6} {f_secret:>5.1f}% {base['cycles'] / iters:>8.0f} "
              f"{base['ipc']:>6.3f} " + line + f"{rej:>4}")
    return rows


def sweep_axis(a, log, axis, values):
    """The mechanism sweeps, public lane ONLY.

    Public lane alone, so nothing the AEAD does can enter the number: this asks
    only what PSTATE.DIT costs the lookup chain, which is the whole of blanket's
    bill in the crossover above.

      q        the predictable fraction. gem5's blanket cost is linear in it and
               so, on the narrow lane, is this machine's.
      tblbits  the table's size. Included because the obvious explanation for a
               flat zero on the wide lane -- "the lane is L1-resident, so there
               is no latency worth predicting" -- is wrong, and this is what
               shows it: the zero holds from 4 KB to 512 KB.
      hdr      the header CONSTANT's width, which is the real axis. Each value
               is a separate binary, because HDR_CONST is a compile-time -D.
    """
    rows = []
    label = {"tblbits": "table", "q": "predictable fraction",
             "hdr": "header value"}[axis]
    lane = a.lane
    print(f"== what blanket DIT costs the PUBLIC lane, by {label} "
          f"(lane={lane}, L={a.mech_L}, q={a.q / 4:.2f}, {a.reps} reps) ==")
    print(f"   {label:>22} {'nodit c/lookup':>15} {'blanket c/lookup':>17} {'blanket':>9} "
          f"{'nodit IPC':>9} {'blk IPC':>9} {'rej':>4} {'n/split':>9}")
    print("   " + "-" * 102)
    for v in values:
        saved = (a.tblbits, a.q, a.lane)
        if axis == "tblbits":
            a.tblbits = v
        elif axis == "q":
            a.q = v
        else:
            a.lane = v
        a.cyc_per_request.pop(a.mech_L, None)
        iters, warm = budget(a, a.mech_L)
        args = common(a, a.mech_L, iters, warm) + ["--nosecret"]
        r = {arm: med(arm, args, a.reps, a.tries, log, f"{axis}{v}.{arm}", a.lane)
             for arm in ("nodit", "blanket")}
        ov = (r["blanket"]["cycles"] - r["nodit"]["cycles"]) / r["nodit"]["cycles"] * 100
        shown = (f"{(1 << v) * 8 // 1024} KB" if axis == "tblbits" else
                 f"{v / 4:.2f}" if axis == "q" else
                 f"{HDR[v][0]} ({HDR[v][1]}b)")
        per = lambda arm: r[arm]["cycles"] / iters / a.mech_L
        rows.append({"axis": axis, "value": v, "label": shown, "lane": a.lane,
                     "tblbits": a.tblbits, "pred_q4": a.q,
                     "L": a.mech_L, "requests": iters,
                     "nodit_cycles": r["nodit"]["cycles"], "blanket_cycles": r["blanket"]["cycles"],
                     "nodit_cyc_per_lookup": round(per("nodit"), 4),
                     "blanket_cyc_per_lookup": round(per("blanket"), 4),
                     "nodit_ipc": round(r["nodit"]["ipc"], 4),
                     "blanket_ipc": round(r["blanket"]["ipc"], 4),
                     "insts": r["nodit"]["insts"],
                     "blanket_pct": round(ov, 2),
                     "ipc_ovh_pct": round((r["nodit"]["ipc"] / r["blanket"]["ipc"] - 1) * 100, 2),
                     "n": r["nodit"]["n"], "n_all": r["nodit"]["n_all"],
                     "n_hi_nodit": r["nodit"]["n_hi"], "n_hi_blanket": r["blanket"]["n_hi"],
                     "lottery_gap": max(r["nodit"]["gap"], r["blanket"]["gap"]),
                     "rejects": r["nodit"]["rejects"] + r["blanket"]["rejects"],
                     "spread_pct": round(max(r["nodit"]["spread_pct"], r["blanket"]["spread_pct"]), 2)})
        split = (f"{r['nodit']['n']}+{r['nodit']['n_hi']}"
                 if r["nodit"]["n_hi"] or r["blanket"]["n_hi"] else str(r["nodit"]["n"]))
        print(f"   {shown:>22} {per('nodit'):>15.3f} {per('blanket'):>17.3f} {ov:>+8.2f}% "
              f"{r['nodit']['ipc']:>9.3f} {r['blanket']['ipc']:>9.3f} {rows[-1]['rejects']:>4} "
              f"{split:>9}")
        a.tblbits, a.q, a.lane = saved
    return rows


def sweep_chunks(a, log, values):
    """What ONE bracketed call costs, measured as a slope instead of a difference.

    `--chunks N` splits a request into N pieces: L/N lookups then one AEAD call
    over a 100/N-byte slice under its own nonce. The public work is the same
    lookups in N runs and the secret bytes are the same bytes in N calls, so a
    PER-CALL placement pays N times and blanket does not. Differencing one arm
    against its NOP twin at a single N gives the bracket's cost mixed with
    whatever else that binary did; the SLOPE of (arm - twin) against N is the
    per-call cost and nothing else, because everything that is not per-call is
    the intercept.

    This is the honest way to quote "one bracket costs X cycles", and it is
    needed here: at one call per request the difference reads 324 cycles when
    the public lane is 10 lookups and ~490 when it is 30 or more, because `sb`
    drains whatever is in flight and a 10-lookup lane has less in it.
    """
    rows = []
    print(f"== what ONE placement costs per call (lane={a.lane}, L={a.mech_L}, "
          f"{a.reps} reps) ==")
    print(f"   arm - its NOP twin, cycles per REQUEST, against calls per request")
    print(f"   {'chunks':>7} {'f_sec':>7} {'base c/req':>11} "
          f"{'bracket - twin':>15} {'per call':>9} {'pass - nop':>12} {'per call':>9} "
          f"{'rej':>4}")
    print("   " + "-" * 84)
    need = ["nodit", "bracket", "bracketnop", "pass", "nop"]
    for n in values:
        saved = a.chunks
        a.chunks = n
        a.cyc_per_request.pop(a.mech_L, None)
        iters, warm = budget(a, a.mech_L)
        args = common(a, a.mech_L, iters, warm)
        r = {arm: med(arm, args, a.reps, a.tries, log, f"chunks{n}.{arm}", a.lane)
             for arm in need}
        pub = med("nodit", args + ["--nosecret"], a.reps, a.tries, log,
                  f"chunks{n}.pub", a.lane)
        base = r["nodit"]
        f_secret = (base["cycles"] - pub["cycles"]) / base["cycles"] * 100
        brk = (r["bracket"]["cycles"] - r["bracketnop"]["cycles"]) / iters
        psw = (r["pass"]["cycles"] - r["nop"]["cycles"]) / iters
        rej = sum(r[x]["rejects"] for x in need) + pub["rejects"]
        rows.append({"axis": "chunks", "value": n, "label": f"{n} calls",
                     "lane": a.lane, "L": a.mech_L, "pred_q4": a.q,
                     "tblbits": a.tblbits, "requests": iters,
                     "f_secret_pct": round(f_secret, 2),
                     "base_cyc_per_req": round(base["cycles"] / iters, 1),
                     "bracket_minus_twin_cyc": round(brk, 1),
                     "bracket_per_call_cyc": round(brk / n, 1),
                     "pass_minus_nop_cyc": round(psw, 1),
                     "pass_per_call_cyc": round(psw / n, 1),
                     "n": base["n"], "n_all": base["n_all"], "rejects": rej,
                     "spread_pct": round(base["spread_pct"], 2)})
        print(f"   {n:>7} {f_secret:>6.1f}% {base['cycles'] / iters:>11.0f} "
              f"{brk:>15.0f} {brk / n:>9.0f} {psw:>12.0f} {psw / n:>9.0f} {rej:>4}")
        a.chunks = saved
    # A cell the gate mostly rejected is a cell whose surviving samples are a
    # biased subset, and it must not be read as a cost. Seen for real, before
    # the budget was sized on the slowest arm: a `pass` cell with 11 rejects
    # against 15 kept read 815 cycles per call where every clean cell read
    # ~1,340.
    clean = [r for r in rows if r["rejects"] <= a.reps // 3]
    dropped = [r["value"] for r in rows if r not in clean]
    if dropped:
        print(f"   NOTE: chunks {dropped} had more than {a.reps // 3} rejected "
              f"samples; read them with suspicion. They stay in the CSV and the "
              f"JSONL.")
    # NO SLOPE IS FITTED, and that is the finding rather than a limitation.
    # (arm - twin) is NOT linear in the number of calls: a bracket around a
    # 100-byte AEAD with a real public lane on either side costs ~490 cycles,
    # and the same bracket around a 4-byte slice with 8 lookups between calls
    # costs ~290. `sb` drains what is in flight, so what one bracket costs
    # depends on what the program had in flight when it hit it. A least-squares
    # slope through those points would report a single "cycles per call" that
    # is true at neither end.
    if len(clean) > 1:
        for key, lab in (("bracket_per_call_cyc", "Apple's bracket"),
                         ("pass_per_call_cyc", "the pass")):
            v = [(r["value"], r[key]) for r in clean]
            lo = min(v, key=lambda t: t[1])
            hi = max(v, key=lambda t: t[1])
            print(f"   {lab}: {hi[1]:.0f} cycles per call at {hi[0]} call(s) per "
                  f"request, {lo[1]:.0f} at {lo[0]}"
                  f"{'  -- NOT constant' if hi[1] > 1.15 * lo[1] else ''}")
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(D, "out"), help="result directory")
    ap.add_argument("--arms", nargs="+", default=None, metavar="ARM",
                    choices=sorted(ARMS),
                    help="restrict the sweep to these arms (default: all nine). "
                         "The gem5 panel runs four -- nodit blanket bracket "
                         "bracketnop -- so that set is how the two instruments "
                         "are made to run the same comparison.")
    ap.add_argument("--bin", default=None,
                    help="directory holding the native_<arm>_<lane> binaries "
                         "(default <this dir>/bin). Point it at a build made "
                         "with a different SECRET_OP to sweep that lane.")
    ap.add_argument("--reps", type=int, default=11)
    ap.add_argument("--tries", type=int, default=8,
                    help="attempts allowed per wanted sample before giving up")
    ap.add_argument("--lane", default="narrow", choices=sorted(HDR),
                    help="which machine's value predictor the public lane is "
                         "aimed at. 'wide' is the canonical gem5 lane "
                         "(HDR_CONST 0x2545F4914F6CDD1D, 62 bits, which this "
                         "machine cannot predict); 'narrow' is 0xCAFEBABE, 32 "
                         "bits, which it can. Default narrow, because that is "
                         "the lane the crossover lives on here.")
    ap.add_argument("--tblbits", type=int, default=9,
                    help="log2 table entries. 9 = 4 KB, the canonical gem5 lane. "
                         "The lane's DIT sensitivity does not depend on this -- "
                         "see --tblbits-sweep, which is why it is 9 and not 16.")
    ap.add_argument("--q", type=int, default=3, help="LVP-predictable fraction * 4")
    ap.add_argument("--L", type=int, nargs="+",
                    default=[10, 50, 200, 1000, 5000, 20000],
                    help="lookups per request; the gem5 sweep's points")
    ap.add_argument("--roi-ms", type=float, default=50.0)
    ap.add_argument("--warm-ms", type=float, default=30.0,
                    help="warmup is sized in TIME: it exists to reach the DVFS "
                         "ceiling, not to warm caches")
    ap.add_argument("--mech-L", type=int, default=20000)
    ap.add_argument("--ins-tol", type=float, default=0.02,
                    help="how far nodit and blanket may differ in retired "
                         "instructions before the run is called invalid, in "
                         "percent. Apple's fixed PMC1 counts kernel entries, so "
                         "two runs of one binary differ by ~0.005%%. It can be "
                         "this tight only because argv is equalised across arms "
                         "(see the argv note in this file); without that the two "
                         "sat at different stack alignments and differed by a "
                         "systematic 0.2%% that was not the mode.")
    ap.add_argument("--tblbits-sweep", nargs="*", type=int, default=None,
                    metavar="BITS", help="run the table-size mechanism sweep instead")
    ap.add_argument("--q-sweep", nargs="*", type=int, default=None,
                    metavar="Q4", help="run the predictable-fraction sweep instead")
    ap.add_argument("--chunks", type=int, default=1,
                    help="secret pieces per request. 1 is the request as the "
                         "gem5 sweeps run it, bit for bit.")
    ap.add_argument("--chunks-sweep", nargs="*", type=int, default=None,
                    metavar="N", help="run the per-call cost sweep instead: "
                                      "(arm - twin) cycles against calls per request")
    ap.add_argument("--hdr-sweep", nargs="*", default=None, metavar="LANE",
                    help="run the header-value-width sweep instead: how many "
                         "value bits this machine's predictor will hold")
    a = ap.parse_args()
    if a.arms:
        # ARM_ORDER drives the manifest, the checksum gate, the rejection
        # tally, the rows and the rotation, so restricting the sweep means
        # restricting it. TWIN entries whose arm or twin is gone go too --
        # a (arm - twin) column with no twin is not a measurement.
        global ARM_ORDER, TWIN
        want = [x for x in ARM_ORDER if x in set(a.arms)]
        missing = sorted(set(a.arms) - set(want))
        if missing:
            sys.exit(f"FATAL: unknown arm(s): {missing}")
        ARM_ORDER = want
        TWIN = {k: v for k, v in TWIN.items() if k in want and v in want}
    if a.bin:
        global BIN
        BIN = os.path.abspath(a.bin)
        if not os.path.isdir(BIN):
            sys.exit(f"FATAL: --bin {BIN} is not a directory")
    a.cyc_per_request = {}
    a.swcost = switch_cost()
    a.cpu = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                           capture_output=True, text=True).stdout.strip()

    a.lane = a.lane
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        sys.exit("this rig is Apple silicon only")
    os.makedirs(a.out, exist_ok=True)
    if os.geteuid() != 0:
        print("note: not root, so the thread cannot be pinned "
              "(kern.sched_thread_bind_cpu). Samples that migrate are rejected by "
              "the implied-clock gate and counted; the reject column is part of "
              "the result.\n")

    which = ("tblbits" if a.tblbits_sweep is not None else
             "q" if a.q_sweep is not None else
             "hdr" if a.hdr_sweep is not None else
             "chunks" if a.chunks_sweep is not None else "crossover")
    stamp = time.strftime("%Y%m%d-%H%M%S")
    jl = os.path.join(a.out, f"runs-{which}-{stamp}.jsonl")
    with open(jl, "w") as log:
        log.write(json.dumps({"provenance": provenance(a)}) + "\n")
        if a.swcost:
            print(f"   one serialising `msr DIT` on this machine: "
                  f"{a.swcost['cyc_per_write']:.2f} cycles "
                  f"({a.swcost['cyc_per_same_value_write']:.2f} for a same-value write)\n")
        t0 = time.time()
        if which == "tblbits":
            vals = a.tblbits_sweep or [9, 11, 12, 13, 14, 15, 16]
            rows = sweep_axis(a, log, "tblbits", vals)
        elif which == "q":
            vals = a.q_sweep or [0, 1, 2, 3, 4]
            rows = sweep_axis(a, log, "q", vals)
        elif which == "chunks":
            rows = sweep_chunks(a, log, a.chunks_sweep or [1, 2, 5, 10, 25])
        elif which == "hdr":
            vals = a.hdr_sweep or [f"b{n}" for n in (8, 16, 24, 32, 34, 35, 36,
                                                     37, 38, 40, 48, 56, 62)]
            rows = sweep_axis(a, log, "hdr", vals)
        else:
            rows = sweep_crossover(a, log)
        el = time.time() - t0
    res = os.path.join(a.out, f"{which}-{stamp}.json")
    tmp = res + ".tmp"
    with open(tmp, "w") as fh:
        json.dump({"provenance": provenance(a), "rows": rows,
                   "elapsed_s": round(el, 1)}, fh, indent=1)
    os.replace(tmp, res)
    print(f"\n   {len(rows)} rows in {el:.0f} s -> {res}")
    print(f"   every run, rejects included -> {jl}")


if __name__ == "__main__":
    main()
