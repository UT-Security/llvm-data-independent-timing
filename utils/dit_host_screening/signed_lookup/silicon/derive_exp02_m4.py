#!/usr/bin/env python3
"""Import the silicon rig's raw JSON into paper_experiments/02's data/ as CSV.

The gem5 half of experiment 02 does the same thing with derive_exp02.py: the
runner writes what it measured, this turns it into the committed CSVs the figure
script reads, and every file carries a provenance header naming the machine, the
compiler, the driver and the binaries. Nothing downstream reads the JSON, and
nothing here recomputes a measurement -- it reshapes and labels.

  python3 derive_exp02_m4.py <out-dir>      # default: ./out/final
"""
import csv
import glob
import json
import os
import sys

D = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(D, "..", "..", "..", ".."))
DATA = os.path.join(REPO, "paper_experiments/02-libsodium-signed-lookup/data")

HDR_TEXT = {"wide": "0x2545F4914F6CDD1D (62 bits)", "narrow": "0xCAFEBABE (32 bits)"}


def load(outdir, kind):
    """Every run of one kind, newest last, keyed so a rerun replaces its cell."""
    out = []
    for p in sorted(glob.glob(os.path.join(outdir, f"{kind}-*.json"))):
        with open(p) as fh:
            out.append((p, json.load(fh)))
    return out


def prov(runs, what):
    """One provenance block for the file, from the runs that produced it."""
    p = runs[0][1]["provenance"]
    sw = p.get("switch_cost") or {}
    lines = [
        f"# provenance: {what}",
        f"# run {p['date']} on {p['host']} ({p['cpu']}, kernel {p['kernel']}); "
        f"{'root, thread pinned' if p['root'] else 'unrooted, thread NOT pinned -- '
           'samples that migrated are rejected by the implied-clock gate and counted '
           'in the rejects column'}",
        f"# clang from llvm-data-independent-timing {p['llvm_commit']}"
        f"{' (WORKING TREE DIRTY)' if p['llvm_dirty'] else ''}; "
        f"driver sha256 {p['driver_sha256_16']}.., byte-identical to gem5-DIT's "
        f"benchmarks/signed_lookup/signed_lookup_gem5.c at the pinned commit, with "
        f"utils/dit_host_screening/signed_lookup/silicon/hdr_const_param.patch applied "
        f"(HDR_CONST becomes a -D; no other change)",
        "# libsodium 1.0.21 --disable-asm -march=armv8.4-a; library arms base / taint / "
        "taintnop from utils/taint_libsodium_arms.sh at the shipped defaults (callee "
        "contract, DIT twins, contract fixpoint seeds, owned list)",
        "# arms: nodit unhardened | blanket the same binary with DIT set before the ROI | "
        "bracket = the unhardened library with Apple's own prologue/epilogue around the "
        "AEAD entry points (mrs DIT token, msr DIT #1, sb, the call, msr DIT #0 only if "
        "it was clear) from utils/dit_host_screening/cioparity/api_bracket.c, which is "
        "what AWS-LC ships | bracketnop its instruction-matched twin (token read -> mov "
        "xzr, both writes and the barrier -> nop, so the restore takes the same branch) | "
        "bracketnobar Apple's sequence minus the speculation barrier, to split the bill; "
        "not a shippable configuration | pass = -ftaint-harden | nop its NOP twin. "
        "dit_cyc_per_req is (arm - twin) cycles per request, which is what the mode "
        "writes cost with layout removed",
        "# instrument: PMC0 (cycles, bare) and PMC1 (instructions, isb-ordered) read from "
        "EL0; CNTVCT_EL0 at 1 GHz gives the implied clock, and a sample outside "
        "3.4-5.0 GHz is rejected as a thread migration and counted",
        f"# lane: HDR_CONST is the value every record header holds. wide = "
        f"{HDR_TEXT['wide']}, the value every gem5 sweep used; narrow = "
        f"{HDR_TEXT['narrow']}. This machine's load value predictor holds 36 bits "
        f"(m4_header_width.csv), so it predicts the narrow header and not the wide one",
    ]
    if sw:
        lines.append(
            f"# one serialising `msr DIT` on this part: {sw.get('cyc_per_write', 0):.2f} "
            f"cycles ({sw.get('cyc_per_same_value_write', 0):.2f} for a same-value "
            f"write), measured by dit_switch_cost.c in this run")
    lines.append(
        "# cycles = median of the FAST cluster of n valid samples. Once the tables "
        "leave L1 a run's cycles come out bimodal ~15% apart on the physical pages the "
        "kernel hands the BSS; n_hi counts the slow-cluster samples, which are kept in "
        "the JSONL and excluded from the median. Below 128 KB of table there is no "
        "split and this is the plain median.")
    return "\n".join(lines) + "\n"


def write(name, header, rows, fields):
    os.makedirs(DATA, exist_ok=True)
    path = os.path.join(DATA, name)
    tmp = path + ".tmp"
    with open(tmp, "w", newline="") as fh:
        fh.write(header)
        w = csv.DictWriter(fh, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow(r)
    os.replace(tmp, path)
    print(f"  {name}: {len(rows)} rows")


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(D, "out", "final")
    if not os.path.isdir(outdir):
        sys.exit(f"no such directory: {outdir}")
    # An optional CSV prefix, so a sweep of a DIFFERENT secret lane does not
    # overwrite the canonical one. `derive_exp02_m4.py <outdir> aes` writes
    # m4_aes_arms.csv beside m4_arms.csv rather than on top of it -- the
    # published chacha numbers stay exactly as measured.
    tag = sys.argv[2] if len(sys.argv) > 2 else ""
    name = lambda base: f"m4_{tag}_{base}" if tag else f"m4_{base}"

    # ------------------------------------------------------------ crossover
    runs = load(outdir, "crossover")
    if runs:
        rows = []
        for _p, j in runs:
            rows += j["rows"]
        order = ["nodit", "blanket", "bracket", "bracketnop", "bracketnobar",
                 "pass", "nop"]
        rows.sort(key=lambda r: (r["lane"] != "narrow", r["L"],
                                 order.index(r["arm"]) if r["arm"] in order else 99))
        write(name("arms.csv"),
              prov(runs, "CANONICAL Apple M4 sweep: secret-fraction crossover, "
                         "both lanes, 7 arms including Apple's own bracket and "
                         "its instruction-matched twin. The paper figure's y is ipc_ovh_pct "
                         "(nodit ipc / arm ipc - 1), the same quantity the gem5 "
                         "figure plots; vs_base_pct is the cycles ratio. pub_* "
                         "columns are the same arm with --nosecret, i.e. the "
                         "PUBLIC lane alone, which is where blanket's whole bill "
                         "comes from"),
              rows,
              ["lane", "L", "pred_q4", "tblbits", "f_secret_pct", "arm", "requests",
               "cycles", "insts", "ipc", "cyc_per_request", "vs_base_pct",
               "ipc_ovh_pct", "ins_vs_base_per_req", "twin", "dit_cyc_per_req",
               "switches_per_req", "pub_cycles", "pub_insts", "pub_ipc",
               "pub_vs_base_pct", "pub_cyc_per_lookup", "n", "n_all", "n_hi",
               "lottery_gap", "rejects", "spread_pct", "warmup", "checksum_pub"])

    # --------------------------------------------------- per-call cost
    runs = load(outdir, "chunks")
    if runs:
        rows = []
        for _p, j in runs:
            rows += j["rows"]
        rows.sort(key=lambda r: (r["L"], r["value"]))
        write("m4_per_call_cost.csv",
              prov(runs, "WHAT ONE PLACEMENT COSTS PER CALL. --chunks N splits a "
                         "request into N pieces, L/N lookups then one AEAD call over "
                         "a 100/N-byte slice under its own nonce: the same public work "
                         "in N runs and the same secret bytes in N calls, so a per-call "
                         "placement pays N times. (arm - its NOP twin) / N is therefore "
                         "the cost of ONE placement with layout removed. Apple's "
                         "bracket lands at 455-510 cycles per call and the pass at "
                         "1,260-1,340. It is not perfectly constant: at 25 calls per "
                         "request with only 8 lookups between them the bracket falls to "
                         "304, because `sb` drains what is in flight and there is less "
                         "in it"),
              rows,
              ["lane", "L", "value", "label", "pred_q4", "tblbits", "requests",
               "f_secret_pct", "base_cyc_per_req", "bracket_minus_twin_cyc",
               "bracket_per_call_cyc", "pass_minus_nop_cyc", "pass_per_call_cyc",
               "n", "n_all", "rejects", "spread_pct"])

    # ------------------------------------------------------- q / hdr / table
    for kind, name, what in (
        ("q", "m4_predictability_sweep.csv",
         "SENSITIVITY: blanket DIT's cost to the PUBLIC lane against q = "
         "pred_q4/4, the fraction of iterations that read the record header. "
         "Linear in q on the narrow lane and identically zero on the wide one, "
         "at both L. gem5's own q sweep is gem5_predictability_sweep.csv"),
        ("hdr", "m4_header_width.csv",
         "THE MECHANISM: the same lane with the header holding N one-bits. "
         "Blanket costs +34/+46% up to N=36 and exactly nothing from N=37, at "
         "both q. That step is the width of this machine's load value "
         "predictor, and it is why the canonical gem5 lane (62 bits) reads zero "
         "here"),
        ("tblbits", "m4_tblbits_sweep.csv",
         "CONTROL: the same lane with the table from 4 KB to 512 KB. The wide "
         "lane's zero is not 'the lane is L1-resident so there is nothing to "
         "predict' -- it holds at every size, while the narrow lane costs at "
         "every size. Above 64 KB the page-mapping lottery splits the samples; "
         "n_hi says how many landed in the slow cluster"),
    ):
        runs = load(outdir, kind)
        if not runs:
            continue
        rows = []
        for _p, j in runs:
            rows += j["rows"]
        rows.sort(key=lambda r: (r["lane"] != "narrow", r["L"], r["pred_q4"],
                                 str(r["value"])))
        write(name, prov(runs, what), rows,
              ["axis", "value", "label", "lane", "L", "pred_q4", "tblbits", "requests",
               "nodit_cycles", "blanket_cycles", "nodit_cyc_per_lookup",
               "blanket_cyc_per_lookup", "nodit_ipc", "blanket_ipc", "insts",
               "blanket_pct", "ipc_ovh_pct", "n", "n_all", "n_hi_nodit",
               "n_hi_blanket", "lottery_gap", "rejects", "spread_pct"])


if __name__ == "__main__":
    print("writing", DATA)
    main()
