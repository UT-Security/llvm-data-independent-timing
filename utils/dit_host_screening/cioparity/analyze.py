#!/usr/bin/env python3
"""Turn run_cio_gem5.py's results into the switch-model decomposition.

Reads results.jsonl and prints, per benchmark:

  ARMS          each arm against `base`, under both switch models. `base` is the
                MIR round-trip control, so these percentages are already free of
                the lowering pipeline's own codegen cost.

  DECOMPOSITION four terms that add up to the pass's total, in the order they
                are incurred:

                  layout       (nop    - base)   inserting the switches, as
                                                 HINT #0, never executing
                  renamed      (taint  - nop)    executing them on a core that
                                                 renames the write
                  serialising  (serdit - spec)   the extra cost of serialising
                                                 it -- what this rig is for
                  total        (taint  - base)   under the serialising model,
                                                 which is what ARM silicon does

  PER SWITCH    the same two terms divided by COMMITTED DIT writes, which is the
                number that transfers to other workloads. Experiment 09 inferred
                ~41 cycles per switch on an M5 by dividing measured cycles by
                measured switches -- a ratio that reproduces its own input by
                construction. Here the numerator is a difference between two
                binaries that differ ONLY in whether the switch executes, so it
                is a measurement rather than a restatement.

  TOGGLE RATE   committed writes per million cycles, to place each benchmark on
                experiment 06's sensitivity curve (+0.66 points at 86 per
                million, +15.80 at 4,601).
"""
import json, statistics, sys, pathlib, argparse

ORDER = ["base", "blanket", "api", "apidsb", "apibare", "apiisb", "apiisbnop", "taint", "taintnop", "taintold", "taintoldnop",
         "taintfn", "taintfnnop", "fine", "finenop"]
NOP_OF = {"taint": "taintnop", "taintold": "taintoldnop", "taintfn": "taintfnnop", "fine": "finenop"}


def load(path):
    rs = [json.loads(l) for l in open(path) if l.strip()]
    return {(r["bench"], r["arm"], r["cfg"]): r for r in rs if not r.get("error")}, rs


def pct(a, b):
    return None if not (a and b) else (a / b - 1.0) * 100.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results", nargs="?",
                    default=str(pathlib.Path.home() /
                                "Documents/libsodium-cioparity-wl/out/results.jsonl"))
    a = ap.parse_args()
    by, allr = load(a.results)
    benches = sorted({k[0] for k in by})
    cfgs = [c for c in ("spec", "serdit") if any(k[2] == c for k in by)]
    arms = [x for x in ORDER if any(k[1] == x for k in by)]

    def cyc(b, arm, c):
        r = by.get((b, arm, c))
        return r["cycles_per_op"] if r else None

    def sw(b, arm, c):
        r = by.get((b, arm, c))
        if not r or r["dit_writes"] is None or not r["roi_n"]:
            return None
        return r["dit_writes"] / r["roi_n"]

    print("=" * 100)
    print("ARMS  cycles/op, and % against `base` (the MIR round-trip control)")
    print("=" * 100)
    hdr = f"{'benchmark':<28}{'arm':<10}"
    for c in cfgs:
        hdr += f"{c+' cyc/op':>16}{c+' %':>11}"
    print(hdr + f"{'switches/op':>13}")
    for b in benches:
        for arm in arms:
            row = f"{b:<28}{arm:<10}"
            for c in cfgs:
                v, base = cyc(b, arm, c), cyc(b, "base", c)
                p = pct(v, base)
                row += f"{v if v else '-':>16}" if not v else f"{v:>16,.0f}"
                row += f"{'-':>11}" if p is None else f"{p:>+10.2f}%"
            s = sw(b, arm, cfgs[-1])
            row += f"{'-':>13}" if s is None else f"{s:>13.1f}"
            print(row)
        print()

    if len(cfgs) == 2:
        print("=" * 100)
        print("DECOMPOSITION  percentage points of `base`, for the shipped `taint` arm")
        print("=" * 100)
        print(f"{'benchmark':<28}{'layout':>11}{'renamed':>11}{'serialising':>13}"
              f"{'total':>11}   {'sw/op':>7}{'wr/Mcyc':>10}{'floor':>9}")
        floors = {}
        for b in benches:
            base = cyc(b, "base", "spec")
            nop, tsp, tsd = (cyc(b, NOP_OF["taint"], "spec"),
                             cyc(b, "taint", "spec"), cyc(b, "taint", "serdit"))
            s = sw(b, "taint", "serdit")
            if not all((base, nop, tsp, tsd)):
                print(f"{b:<28}  incomplete"); continue
            # NOISE FLOOR. `base` executes no DIT, so the switch model cannot
            # affect it and any spec-vs-serdit difference here is measurement
            # drift, not signal. It bounds every other column in the row: a
            # serialising term below the floor means nothing. chacha20 reads
            # 0.00 because its driver seeds key and nonce from rand(); ed25519
            # calls crypto_sign_keypair inside the loop, so every region signs
            # with fresh OS entropy and gem5 cannot replay it.
            bsd = cyc(b, "base", "serdit")
            floor = abs(bsd - base) / base * 100 if bsd else None
            floors[b] = floor
            layout = (nop - base) / base * 100
            renamed = (tsp - nop) / base * 100
            serial = (tsd - tsp) / base * 100
            total = (tsd - base) / base * 100
            # Writes per million BASELINE cycles, i.e. per unit of WORK -- not
            # per hardened cycle. It has to be the baseline denominator for the
            # linear model to close and for the number to be comparable with
            # experiment 06's sensitivity curve (+0.66 points at 86 per million,
            # +15.80 at 4,601). Against the hardened denominator the rate is
            # diluted by the very overhead it is meant to predict: chacha20
            # encrypt reads 23,371 that way and 41,593 this way, and only the
            # second reproduces the measured +80 points as rate x cycles/switch.
            rate = (s / base * 1e6) if s else 0
            fl = f"{floor:>8.2f}%" if floor is not None else f"{'-':>9}"
            print(f"{b:<28}{layout:>+10.2f}{renamed:>+10.2f}{serial:>+12.2f}{total:>+10.2f}"
                  f"   {s:>7.1f}{rate:>10.0f}{fl}")
        print()
        for b, f in floors.items():
            if f is not None and f > 0.05:
                print(f"  NOTE {b}: control drift {f:.2f}% -- treat any term below that as zero.")
        print()
        print("=" * 100)
        print("PER SWITCH  cycles, from a difference between binaries that differ only")
        print("            in whether the switch executes -- not a ratio of one measurement")
        print("=" * 100)
        print(f"{'benchmark':<28}{'renamed':>12}{'serialising':>14}{'ratio':>10}")
        rn, sr = [], []
        for b in benches:
            nop, tsp, tsd = (cyc(b, NOP_OF["taint"], "spec"),
                             cyc(b, "taint", "spec"), cyc(b, "taint", "serdit"))
            s = sw(b, "taint", "serdit")
            if not all((nop, tsp, tsd)) or not s:
                continue
            a1, a2 = (tsp - nop) / s, (tsd - nop) / s
            rn.append(a1); sr.append(a2)
            # A ratio against a renamed cost of ~0 is meaningless -- dividing by
            # 0.1 cycles manufactured a "-135x" on the first run of this script.
            # Below one cycle per switch the renamed term IS zero, so report the
            # absolute costs and say so instead of printing a ratio.
            rat = f"{a2/a1:>9.1f}x" if abs(a1) >= 1.0 else f"{'~0 base':>10}"
            print(f"{b:<28}{a1:>12.1f}{a2:>14.1f}{rat}")
        if len(rn) > 1:
            mr, ms = statistics.median(rn), statistics.median(sr)
            rat = f"{ms/mr:>9.1f}x" if abs(mr) >= 1.0 else f"{'~0 base':>10}"
            print(f"{'median':<28}{mr:>12.1f}{ms:>14.1f}{rat}")
        print()
        print("  The transferable claim is the CONSISTENCY of these columns across")
        print("  independent benchmarks, not any single row.")
        print()
        print("  NOT a closure check. rate x cost reproduces the measured total")
        print("  identically -- rate is switches/base-cycles and cost is")
        print("  delta-cycles/switches, so the product is delta-cycles/base-cycles")
        print("  by algebra, whatever the numbers. Printing it as agreement would")
        print("  be the same self-reproducing ratio experiment 09 warns about.")
        print()
        print("  Independent reference points for the serialising column:")
        print("    34.3 cyc/write  experiment 06, gem5 Neoverse-V2, mbedTLS record MAC")
        print("    76.3 cyc/write  experiment 06, same workload at a 53x lower rate")
        print("    ~41 cyc/switch  experiment 09, Apple M5 silicon, this library")
        print("    ~24 cyc/switch  experiment 02, Apple M5 silicon")

    if len(cfgs) == 2:
        print("=" * 100)
        print("PER-POLICY LAYOUT vs SWITCHES  each policy against ITS OWN nop arm")
        print("=" * 100)
        print("  layout  = (policyNOP - base)      code layout alone, no switch executes")
        print("  renamed = (policy - policyNOP)    the switches executing, renamed")
        print("  serial  = (policy@serdit - policy@spec)")
        print("  A policy with a large NEGATIVE layout term is winning on the codegen")
        print("  lottery, not on placement. That is not a ranking of placement quality.")
        print()
        print(f"{'benchmark':<26}{'policy':<10}{'layout':>9}{'renamed':>9}{'serial':>9}"
              f"{'total':>9}{'sw/op':>7}{'dwell':>8}")
        for b in benches:
            base = cyc(b, "base", "spec")
            if not base:
                continue
            for pol, nop in NOP_OF.items():
                n, ps, pd = cyc(b, nop, "spec"), cyc(b, pol, "spec"), cyc(b, pol, "serdit")
                if not all((n, ps, pd)):
                    continue
                r = by.get((b, pol, "serdit"))
                dw = ""
                if r and r.get("dit_cycles") and r.get("cycles_total"):
                    dw = f"{r['dit_cycles'] / r['cycles_total'] * 100:.0f}%"
                print(f"{b:<26}{pol:<10}{(n/base-1)*100:>+8.2f}{(ps-n)/base*100:>+9.2f}"
                      f"{(pd-ps)/base*100:>+9.2f}{(pd/base-1)*100:>+9.2f}"
                      f"{(sw(b, pol, 'serdit') or 0):>7.0f}{dw:>8}")
            print()

    errs = [r for r in allr if r.get("error")]
    if errs:
        print(f"\n{len(errs)} run(s) did not complete:")
        for r in errs:
            print(f"  {r['bench']}/{r['arm']}/{r['cfg']}: {r['error']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
