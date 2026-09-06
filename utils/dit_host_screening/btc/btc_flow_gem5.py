#!/usr/bin/env python3
"""The wallet flow under gem5: coin selection, then K signatures, K swept.

Experiment 01's crossover is a silicon result: inside one CreateTransaction
call, coin selection (public) then one CKey::Sign per input (secret), with the
input count K moving only the secret fraction. Under gem5 the two lanes were
measured apart (btc_gem5.py --bench coinsel|sign). This runner drives
btc_flow_gem5.cpp, which puts the same two lanes in one ROI with the same knob,
under both switch models - the renamed `msr DIT` counterfactual that silicon
cannot give.

Arms (benchmarks/bitcoin/build_flow_arms.sh):
    base     selection + libsecp256k1 built through the pass, empty seed
    blanket  base + a constructor that sets DIT before main
    taint    selection + libsecp256k1 built with seed9.txt

Per K, median over --offsets argv[0] lengths (gem5 SE puts the binary path on
the initial stack, so its LENGTH moves every stack address; dit-gem5-rig-traps
#5), and reported with the spread:

    f_secret(K)        = 1 - base(0) / base(K)         measured via K = 0,
                                                       as BTC_BENCH_SIGN=0 is
    C_public           = blanket(0) / base(0) - 1      the prize, isolated
    C_whole(K)         = blanket(K) / base(K) - 1
    pass vs blanket(K) = taint(K) / blanket(K) - 1     the crossover column,
                                                       per switch model
    switch cost(K)     = taint(serdit) / taint(spec) - 1

Gates, per K and offset: one checksum across every arm and model; simInsts
identical across models per arm; base's cycles identical across models;
ditSuppressed 0 in base.

Usage:
    btc_flow_gem5.py [--K 0,1,4,10,25,50,100,200,400] [--offsets 5] [--jobs N]
                     [--tag flow] [--resume]
"""
import argparse, csv, hashlib, os, pathlib, re, shutil, statistics as st
import subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import btc_gem5 as g  # GEM5, CONFIG, BIN, CONFIGS, first_dump, pick

ARMS = {"base": "btc_flow_base", "blanket": "btc_flow_blanket",
        "api": "btc_flow_api",          # the Apple bracket on the two secret entry points
        "apinop": "btc_flow_apinop",    # its instruction-matched NOP twin
        "taint": "btc_flow_taint",
        "taintx": "btc_flow_taintx"}   # the pass with -taint-dit-external-preserves
UNPROTECTED = ("base", "apinop")


def stage(outroot, K, off, arm, cfg, binary):
    """Equal-length path within an offset, unique per run (see btc_gem5.canon_path).
    The file name is `b` repeated offset+1 times, so offsets differ by exactly
    one byte of argv[0] and every arm at one offset shares a length."""
    slot = hashlib.md5(f"{outroot}/K{K}/o{off}/{arm}/{cfg}".encode()).hexdigest()[:8]
    d = g.BIN / "armlink" / slot
    d.mkdir(parents=True, exist_ok=True)
    c = d / ("b" * (off + 1))
    if c.exists():
        c.unlink()
    try:
        os.link(g.BIN / binary, c)
    except OSError:
        shutil.copy2(g.BIN / binary, c)
    return c


def done(d):
    sp, lg = d / "stats.txt", d / "run.log"
    return (sp.exists() and sp.stat().st_size > 0 and lg.exists()
            and "checksum=" in lg.read_text(errors="replace"))


def run_one(job):
    K, off, arm, cfg, outroot, a = job
    d = outroot / f"K{K}" / f"o{off}" / f"{arm}__{cfg}"
    rec = {"K": K, "offset": off, "arm": arm, "cfg": cfg,
           "iter": a.iter, "warmup": a.warmup, "targets": a.targets, "wall_s": ""}
    if a.resume and done(d):
        rec["rc"] = 0
    else:
        d.mkdir(parents=True, exist_ok=True)
        cmd = [str(g.GEM5), "-d", str(d), str(g.CONFIG)] + g.CONFIGS[cfg] + [
            "--binary", str(stage(outroot, K, off, arm, cfg, ARMS[arm])),
            "--arguments", f"{a.iter} {a.warmup} {K} {a.targets}"]
        t0 = time.time()
        with open(d / "run.log", "w") as log:
            rec["rc"] = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT).returncode
        rec["wall_s"] = round(time.time() - t0, 1)
    sp = d / "stats.txt"
    if rec["rc"] == 0 and sp.exists():
        s = g.first_dump(sp)
        rec.update({
            "cycles": g.pick(s, "core.numCycles", "numCycles"),
            "numInsts": g.pick(s, "core.commitStats0.numInsts", "commitStats0.numInsts"),
            "simInsts": g.pick(s, "simInsts"),
            "ipc": g.pick(s, "core.ipc"),
            "ditSuppressed": g.pick(s, "ditSuppressed") or 0.0,
            "ditSwitches": g.pick(s, "commit.ditWrites") or 0.0,
            "commitNonSpecStalls": g.pick(s, "commitNonSpecStalls") or 0.0,
        })
    lg = d / "run.log"
    m = re.search(r"checksum=(\d+)", lg.read_text(errors="replace")) if lg.exists() else None
    rec["checksum"] = m.group(1) if m else ""
    print(f"  K={K:<4} o{off} {arm:<8} {cfg:<7} rc={rec['rc']} cycles={rec.get('cycles')} "
          f"ck={rec['checksum']} {rec['wall_s']}s", flush=True)
    return rec


COLS = ["K", "offset", "arm", "cfg", "rc", "iter", "warmup", "targets", "cycles",
        "numInsts", "simInsts", "ipc", "ditSuppressed", "ditSwitches",
        "commitNonSpecStalls", "checksum", "wall_s"]


def pct(a, b):
    return (a / b - 1) * 100


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--K", default="0,1,4,10,25,50,100,200,400")
    ap.add_argument("--arms", default=",".join(ARMS))
    ap.add_argument("--configs", default="spec,serdit")
    ap.add_argument("--offsets", type=int, default=5)
    ap.add_argument("--iter", type=int, default=1)
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--targets", type=int, default=1)
    ap.add_argument("--jobs", type=int, default=100)
    ap.add_argument("--out", default=str(pathlib.Path.home() / "Documents/dit-browser-bench/gem5-btc"))
    ap.add_argument("--tag", default="flow")
    ap.add_argument("--resume", action="store_true")
    a = ap.parse_args()

    for p in (g.GEM5, g.CONFIG, g.BIN):
        if not p.exists():
            sys.exit(f"missing: {p}")
    Ks = [int(k) for k in a.K.split(",")]
    arms, cfgs = a.arms.split(","), a.configs.split(",")
    if 0 not in Ks:
        print("NOTE: K=0 absent, so f_secret and C_public cannot be derived")
    outroot = pathlib.Path(a.out) / a.tag
    outroot.mkdir(parents=True, exist_ok=True)

    # Largest K first: those runs are the longest, and packing them early
    # keeps the tail of the sweep short.
    jobs = [(K, off, arm, cfg, outroot, a)
            for K in sorted(Ks, reverse=True) for off in range(a.offsets)
            for arm in arms for cfg in cfgs]
    print(f"gem5 flow [{a.tag}]: {len(jobs)} runs, {a.jobs} parallel, "
          f"K={Ks} offsets={a.offsets} arms={arms} configs={cfgs}")
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        recs = list(ex.map(run_one, jobs))

    with open(outroot / "flow_runs.csv", "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=COLS, extrasaction="ignore")
        w.writeheader()
        w.writerows(sorted(recs, key=lambda r: (r["K"], r["offset"], r["arm"], r["cfg"])))
    print("WROTE", outroot / "flow_runs.csv")

    # ---- gates ---------------------------------------------------------
    ok = True
    by = {(r["K"], r["offset"], r["arm"], r["cfg"]): r for r in recs}
    failed = [r for r in recs if r["rc"] != 0 or not r.get("cycles")]
    if failed:
        ok = False
        print(f"\nGATE FAIL: {len(failed)} run(s) without a result: "
              f"{[(r['K'], r['offset'], r['arm'], r['cfg']) for r in failed][:12]}")
    print("\n--- gates per K and offset ---")
    for K in Ks:
        for off in range(a.offsets):
            cell = [by[k] for k in by if k[0] == K and k[1] == off and by[k].get("cycles")]
            if not cell:
                continue
            msgs = []
            if len({r["checksum"] for r in cell}) != 1:
                msgs.append("checksums differ")
            for arm in arms:
                si = {r["simInsts"] for r in cell if r["arm"] == arm}
                if len(si) > 1:
                    msgs.append(f"{arm} simInsts differ across models")
            for arm in UNPROTECTED:
                cy = {r["cycles"] for r in cell if r["arm"] == arm}
                if len(cy) > 1:
                    msgs.append(f"{arm} cycles differ across models")
                if any(r["ditSuppressed"] for r in cell if r["arm"] == arm):
                    msgs.append(f"{arm} ditSuppressed != 0")
            if msgs:
                ok = False
            print(f"  K={K:<4} o{off}  {'OK' if not msgs else 'FAIL: ' + '; '.join(msgs)}")

    # ---- derived: medians over offsets ----------------------------------
    def med(K, arm, cfg):
        v = [by[(K, o, arm, cfg)]["cycles"] for o in range(a.offsets)
             if (K, o, arm, cfg) in by and by[(K, o, arm, cfg)].get("cycles")]
        return st.median(v) if v else None

    def spread(K, arm, cfg):
        v = [by[(K, o, arm, cfg)]["cycles"] for o in range(a.offsets)
             if (K, o, arm, cfg) in by and by[(K, o, arm, cfg)].get("cycles")]
        return pct(max(v), min(v)) if len(v) > 1 else 0.0

    def per_offset(K, num, den, cfg):
        return [pct(by[(K, o, num, cfg)]["cycles"], by[(K, o, den, cfg)]["cycles"])
                for o in range(a.offsets)
                if (K, o, num, cfg) in by and (K, o, den, cfg) in by
                and by[(K, o, num, cfg)].get("cycles") and by[(K, o, den, cfg)].get("cycles")]

    ref = cfgs[0]
    base0 = med(0, "base", ref) if 0 in Ks else None
    blanket0 = med(0, "blanket", ref) if 0 in Ks and "blanket" in arms else None
    rows = []
    for K in Ks:
        b = med(K, "base", ref)
        if not b:
            continue
        row = {"K": K,
               "f_secret_pct": (1 - base0 / b) * 100 if base0 else "",
               "base_cycles": b,
               "C_public_pct": pct(blanket0, base0) if base0 and blanket0 else ""}
        if "blanket" in arms and med(K, "blanket", ref):
            row["C_whole_pct"] = pct(med(K, "blanket", ref), b)
        for cfg in cfgs:
            t, bl = med(K, "taint", cfg), med(K, "blanket", cfg)
            if t:
                row[f"pass_vs_base_{cfg}_pct"] = pct(t, b)
            if t and bl:
                po = per_offset(K, "taint", "blanket", cfg)
                row[f"pass_vs_blanket_{cfg}_pct"] = pct(t, bl)
                row[f"pass_vs_blanket_{cfg}_min"] = min(po) if po else ""
                row[f"pass_vs_blanket_{cfg}_max"] = max(po) if po else ""
        if all(c in cfgs for c in ("spec", "serdit")) and med(K, "taint", "spec"):
            row["switch_cost_pct"] = pct(med(K, "taint", "serdit"), med(K, "taint", "spec"))
        row["spread_max_pct"] = max(spread(K, arm, cfg) for arm in arms for cfg in cfgs)
        rows.append(row)

    dcols = ["K", "f_secret_pct", "base_cycles", "C_public_pct", "C_whole_pct"]
    for cfg in cfgs:
        dcols += [f"pass_vs_base_{cfg}_pct", f"pass_vs_blanket_{cfg}_pct",
                  f"pass_vs_blanket_{cfg}_min", f"pass_vs_blanket_{cfg}_max"]
    dcols += ["switch_cost_pct", "spread_max_pct"]
    with open(outroot / "flow_derived.csv", "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=dcols, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    print("WROTE", outroot / "flow_derived.csv")

    def f(x, w=8):
        return f"{x:>{w}.2f}" if isinstance(x, float) else f"{str(x):>{w}}"

    print(f"\n--- the flow, median over {a.offsets} offsets; pass vs blanket: negative = the pass wins ---")
    hdr = f"{'K':>4} {'f_secret':>9} {'C_public':>9} {'C_whole':>8}"
    for cfg in cfgs:
        hdr += f" {'pass/blanket ' + cfg:>21} {'[min..max]':>17}"
    hdr += f" {'switch':>7} {'spread':>7}"
    print(hdr)
    for r in rows:
        line = f"{r['K']:>4} {f(r['f_secret_pct'])}% {f(r['C_public_pct'])}% {f(r.get('C_whole_pct', ''))}%"
        for cfg in cfgs:
            line += (f" {f(r.get(f'pass_vs_blanket_{cfg}_pct', ''), 20)}%"
                     f" [{f(r.get(f'pass_vs_blanket_{cfg}_min', ''), 6)}..{f(r.get(f'pass_vs_blanket_{cfg}_max', ''), 6)}]")
        line += f" {f(r.get('switch_cost_pct', ''), 6)}% {f(r['spread_max_pct'], 6)}%"
        print(line)

    print("\nGATES", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
