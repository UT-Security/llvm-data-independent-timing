#!/usr/bin/env python3
"""IPC under blanket DIT vs no DIT, for the Bitcoin wallet call, on silicon.

macOS exposes no usable PMU (no perf; kpc/kperf are private and need root; this
box has Command Line Tools, so no xctrace), so retired-instruction and cycle
counts cannot be read directly. Absolute IPC is therefore NOT obtainable on this
machine -- but the CHANGE in IPC is, exactly, and without a counter:

  The `baseline` and `always` arms are the SAME BINARY (build-nodit-v2). They
  differ only in DYLD_INSERT_LIBRARIES=dit_on.dylib, which sets PSTATE.DIT
  process-wide. The instruction stream is therefore identical, which gem5
  confirms on the same benchmark family to the instruction:

      coinsel base    simInsts 59,678,264   numInsts 59,697,945
      coinsel blanket simInsts 59,678,264   numInsts 59,697,945

  With I fixed, IPC = I/C, so

      IPC_blanket / IPC_base = C_base / C_blanket = t_base / t_blanket
                             = 1 / (1 + C_whole)

  i.e. the IPC loss is the time cost, renormalised. No counter needed.

The one assumption is equal clock frequency between arms. The duplicate-baseline
arm bounds it: baseline2/baseline is two instruction-identical arms, so whatever
it reads is the combined frequency-drift and noise budget, and it is reported
here per knob point as the uncertainty on each IPC figure.

Usage: ipc_wallet_sweep.py [csv]
"""
import csv, os, statistics as st, sys

HERE = os.path.dirname(os.path.abspath(__file__))
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "wallet_sweep.csv")
rows = list(csv.DictReader(open(path)))
KS = sorted({int(r["inputs"]) for r in rows})


def series(K, arm):
    return {int(r["rep"]): float(r["ns_per_op"])
            for r in rows if int(r["inputs"]) == K and r["arm"] == arm}


def ratio(K, a, b):
    """median per-rep ratio b/a, and n slower / N."""
    A, B = series(K, a), series(K, b)
    reps = sorted(set(A) & set(B))
    if not reps:
        return None
    r = [B[x] / A[x] for x in reps]
    return st.median(r), sum(1 for x in r if x > 1.0), len(r)


print(f"IPC under blanket DIT, {os.path.basename(path)}")
print("WalletCreateTxUsePresetInputsAndCoinSelection, Apple M5 (Mac17,2), "
      "same binary both arms\n")
print(f"{'K':>5} {'f_secret':>9} {'base ms':>9} {'blanket ms':>11} "
      f"{'time cost':>10} {'IPC change':>11} {'+/- drift':>10} {'n/N':>7}")
print("-" * 80)

for K in KS:
    base, always = series(K, "baseline"), series(K, "always")
    pub = series(K, "pub_base")
    reps = sorted(set(base) & set(always))
    if not reps:
        continue
    f = (st.median([(base[r] - pub[r]) / base[r] for r in sorted(set(base) & set(pub))]) * 100
         if pub else float("nan"))
    cw, n, N = ratio(K, "baseline", "always")
    d_ipc = (1.0 / cw - 1.0) * 100          # exact, given identical instructions
    fl = ratio(K, "baseline", "baseline2")
    drift = abs(fl[0] - 1) * 100 if fl else float("nan")
    print(f"{K:>5} {f:>8.1f}% {st.median(base.values())/1e6:>9.2f} "
          f"{st.median(always.values())/1e6:>11.2f} {(cw-1)*100:>+9.2f}% "
          f"{d_ipc:>+10.2f}% {drift:>9.2f}% {n:>3}/{N}")

print("\nIPC change = 1/(1 + time cost) - 1, exact because the instruction stream")
print("is identical between arms. Negative = blanket DIT retires fewer")
print("instructions per cycle. '+/- drift' is the duplicate-baseline arm, the")
print("frequency-drift and noise budget on that figure.")
# Absolute silicon IPC is measurable after all (kperf fixed counters, root;
# ipc.md section 1), and the gem5 figures for the same code live with their
# gates in paper_experiments/01-bitcoin-core-wallet/{ipc.md,data/gem5/} rather
# than here, where an earlier footer kept quoting pre-argv[0]-gate numbers.
print("\nAbsolute IPC on this machine needs the kperf fixed counters (root; ipc.md")
print("section 1). gem5 absolute IPC for the same code: btc_gem5.py --bench")
print("coinsel|sign, quoted from the equal-length-argv[0] runs recorded in ipc.md.")
