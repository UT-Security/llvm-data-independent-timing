#!/usr/bin/env python3
"""Import a signed-lookup gem5 reproduction into paper_experiments/02's data/.

Reads the runner's outputs from WORK (see benchmarks/signed_lookup/run_gem5.py
in gem5-DIT) and writes, each with a provenance header:

  data/gem5_arms.csv                   canonical lane (q4=3), 5 offsets   <- WORK/gem5_arms_off5.csv
  data/gem5_value_predictor.csv        public lane alone, same sweep      <- WORK/gem5_value_predictor_off5.csv
  data/gem5_predictability_sweep.csv   q = 0..1 at L=200 and 20000        <- WORK/gem5_arms_off5_q0-1-2-3-4.csv
  data/gem5_arms_pure_hash_chase.csv   q=0, full L                        <- WORK/gem5_arms_off5_q0.csv
  data/gem5_arms_q50.csv               q=0.5, full L                      <- WORK/gem5_arms_off5_q2.csv
  data/gem5_value_predictor_by_arm.csv what the value predictor did under each arm, from WORK/runs/*/stats.txt
  data/gem5_stack_offset_spread.csv    cycles at each of the 5 offsets per cell, from WORK/results_off5.json

Frozen files it never touches, because no committed driver can regenerate them:
gem5_arms_ed25519.csv, gem5_arms_constant_chain.csv, gem5_stack_offset_sensitivity.csv.

Env: WORK (default ~/Documents/signed_lookup-gem5), G5 (default: this repo's submodule),
LLVM_BUILD (for the provenance line only).
"""
import csv, datetime, json, os, pathlib, platform, subprocess, sys

R = pathlib.Path(__file__).resolve().parents[3]
DATA = R / "paper_experiments/02-libsodium-signed-lookup/data"
WORK = pathlib.Path(os.environ.get("WORK", pathlib.Path.home() / "Documents/signed_lookup-gem5"))
G5 = pathlib.Path(os.environ.get("G5", R / "gem5-DIT"))
LB = pathlib.Path(os.environ.get("LLVM_BUILD", R / "build"))
sys.path.insert(0, str(G5 / "benchmarks/signed_lookup")); from run_gem5 import dumps  # noqa: E402


def sha(repo):
    try:
        s = subprocess.run(["git", "-C", str(repo), "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
        dirty = subprocess.run(["git", "-C", str(repo), "status", "--short"], capture_output=True, text=True).stdout.strip()
        return s + ("+dirty" if dirty else "")
    except OSError:
        return "?"


gem5_bin = G5 / "build/ARM/gem5.fast"
PROV = (f"# provenance: run {datetime.date.today()} on {platform.machine()} {platform.system()} ({platform.node()}), native static build; "
        f"gem5-DIT {sha(G5)}; clang from llvm-data-independent-timing {sha(LB.parent)} ({LB}); gem5.fast built "
        f"{datetime.datetime.fromtimestamp(gem5_bin.stat().st_mtime):%Y-%m-%d %H:%M} ; WORK={WORK}")


def imp(src, dst, note):
    lines = (WORK / src).read_text().splitlines()
    (DATA / dst).write_text("\n".join([PROV, f"# {note}"] + lines) + "\n")
    print(f"  data/{dst}  ({sum(1 for l in lines if not l.startswith('#')) - 1} rows)")


imp("gem5_arms_off5.csv", "gem5_arms.csv",
    "CANONICAL lane: hashed chase, LVP-predictable header on 3 of 4 iterations (pred_q4=3), chacha20-poly1305 secret op. vs_base_pct is cycles; the paper figure uses IPC overhead = nodit ipc / arm ipc - 1 from the ipc column.")
imp("gem5_value_predictor_off5.csv", "gem5_value_predictor.csv",
    "CANONICAL lane, public lane alone (--nosecret): value predictor totals with and without DIT.")
imp("gem5_arms_off5_q0-1-2-3-4.csv", "gem5_predictability_sweep.csv",
    "SENSITIVITY: the LVP-predictable fraction q = pred_q4/4 swept 0..1 at L=200 and L=20000. Blanket's public-lane cost is linear in q; the canonical lane fixes q=0.75.")
imp("gem5_arms_off5_q0.csv", "gem5_arms_pure_hash_chase.csv",
    "Pure hashed chase, q=0 (no LVP-predictable loads): blanket is free on the public lane.")
imp("gem5_arms_off5_q2.csv", "gem5_arms_q50.csv",
    "Full L sweep at q=0.5 (pred_q4=2).")

# ---- what the value predictor did under each arm (canonical lane, offset 0)
rows = [r for r in csv.DictReader(l for l in (WORK / "gem5_arms_off5.csv").read_text().splitlines() if not l.startswith("#"))]
req = {int(r["L"]): int(r["requests"]) for r in rows if r["arm"] == "nodit"}
f = {int(r["L"]): r["f_secret_pct"] for r in rows if r["arm"] == "nodit"}
q = int(rows[0]["pred_q4"]); K = "board.processor.cores.core.valuePredictor."
out = [["L", "pred_q4", "f_secret_pct", "arm", "requests", "predictions", "pred_correct", "load_predictions", "load_correct",
        "alu_predictions", "stride_predictions", "vtage_predictions", "load_predictions_per_request", "cycles", "insts"]]
for L in sorted(req):
    for arm, name in (("base", "nodit"), ("blanket", "blanket"), ("taint", "pass")):
        b = dumps(WORK / "runs" / f"L{L}_q{q}_{arm}_renamed" / "stats.txt")[0]
        g = lambda k: int(b.get(K + k, 0))  # noqa: E731
        out.append([L, q, f[L], name, req[L], g("predictions"), g("predCorrect"), g("loadPredictions"), g("loadPredCorrect"),
                    g("aluPredictions"), g("stridePredictions"), g("vtagePredictions"), f"{g('loadPredictions') / req[L]:.1f}",
                    int(b["board.processor.cores.core.numCycles"]), int(b["board.processor.cores.core.commitStats0.numInsts"])])
with open(DATA / "gem5_value_predictor_by_arm.csv", "w", newline="") as fh:
    fh.write(PROV + "\n# CANONICAL lane, full flow, offset-0 run per point: what the value predictor did under each arm (renamed switch model; the counts do not depend on it). Blanket's load_predictions is 0 at every L; the pass keeps the public lane's.\n")
    csv.writer(fh).writerows(out)
print(f"  data/gem5_value_predictor_by_arm.csv  ({len(out) - 1} rows)")

# ---- cycles at every offset, per cell (the spread behind every median)
res = json.loads((WORK / "results_off5.json").read_text())
out = [["L", "pred_q4", "arm", "switch", "nosecret", "offset", "argv0_extra_bytes", "cycles", "insts"]]
for r in sorted((r for r in res if "cycles" in r), key=lambda r: (r["L"], r["q"], r["arm"], r["model"], r["nosign"], r["off"])):
    out.append([r["L"], r["q"], {"base": "nodit", "blanket": "blanket", "taint": "pass"}[r["arm"]], r["model"], int(r["nosign"]), r["off"], r["off"], int(r["cycles"]), int(r["insts"])])
with open(DATA / "gem5_stack_offset_spread.csv", "w", newline="") as fh:
    fh.write(PROV + "\n# CANONICAL lane: the cycles behind every median, one row per (cell, stack offset). offset k lengthens argv[0] by k bytes; gem5 SE puts argv[0] on the initial stack, so this is the layout lottery the medians average over.\n")
    csv.writer(fh).writerows(out)
print(f"  data/gem5_stack_offset_spread.csv  ({len(out) - 1} rows)")
