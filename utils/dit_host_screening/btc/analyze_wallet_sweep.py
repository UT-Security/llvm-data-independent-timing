#!/usr/bin/env python3
"""Analyse the Bitcoin wallet secret-fraction sweep.

Paired per rep, arm order rotated in the rig, so the headline for every cell is
the median per-rep RATIO plus the sign-test count n/N. On a benchmark whose
between-process spread can dwarf the effect, n/N is what says whether a median
means anything.

Reports, per knob setting:

  f_secret   (baseline - pub_base) / baseline        MEASURED, via BTC_BENCH_SIGN=0
  C_public   pub_always / pub_base - 1               blanket DIT, public lane alone
  C_whole    always / baseline - 1                   blanket DIT, whole call
  pass       pass / baseline - 1
  crossover  pass / always - 1                       NEGATIVE = the pass wins
  switches   pass / passnop - 1                      the MSR DIT's own cost
  floor      baseline2 / baseline - 1                noise floor

Then the closure check. The three measured quantities are related by

    C_whole ~= (1 - f)*C_public + f*C_secret

so C_secret is over-determined: solving at every knob setting must give the same
answer, and that answer must match the independently measured
SignTransactionECDSA figure (+3.39%). If the implied C_secret wanders or lands
far from it, the arms are not measuring what their names claim. This is the
framework's "region and whole-program arithmetic must close" detector applied
across a sweep rather than at one point.
"""
import csv, os, statistics as st, sys

HERE = os.path.dirname(os.path.abspath(__file__))
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "wallet_sweep.csv")
rows = list(csv.DictReader(open(path)))
if not rows:
    sys.exit("no rows")

KS = sorted({int(r["inputs"]) for r in rows})
C_SECRET_REF = 3.39  # SignTransactionECDSA, 26/40, dit-bitcoin-sign-two-instruments.md


def series(K, arm):
    return {int(r["rep"]): float(r["ns_per_op"])
            for r in rows if int(r["inputs"]) == K and r["arm"] == arm}


def paired(K, a, b):
    """median per-rep ratio b/a as a percent, plus n/N slower."""
    A, B = series(K, a), series(K, b)
    reps = sorted(set(A) & set(B))
    if not reps:
        return None
    rat = sorted(B[r] / A[r] for r in reps)
    return (st.median(rat) - 1) * 100, sum(1 for x in rat if x > 1.0), len(reps)


def cell(p):
    return "     -    " if p is None else f"{p[0]:+7.2f}% {p[1]:>2}/{p[2]}"


print(f"benchmark: WalletCreateTxUsePresetInputsAndCoinSelection   "
      f"reps {len(series(KS[0], 'baseline'))}\n")
print(f"{'K':>5} {'f_secret':>9} {'base ms':>9} | {'C_public':>13} {'C_whole':>13} "
      f"{'pass':>13} {'CROSSOVER':>13} {'switches':>13} {'floor':>13}")
print("-" * 118)

table = []
for K in KS:
    base, pub = series(K, "baseline"), series(K, "pub_base")
    reps = sorted(set(base) & set(pub))
    f = st.median([(base[r] - pub[r]) / base[r] for r in reps]) * 100 if reps else float("nan")
    cpub = paired(K, "pub_base", "pub_always")
    cwh = paired(K, "baseline", "always")
    table.append((K, f, cpub, cwh))
    print(f"{K:>5} {f:>8.2f}% {st.median(base.values())/1e6:>9.2f} | "
          f"{cell(cpub)} {cell(cwh)} {cell(paired(K,'baseline','pass'))} "
          f"{cell(paired(K,'always','pass'))} {cell(paired(K,'passnop','pass'))} "
          f"{cell(paired(K,'baseline','baseline2'))}")

# ---- crossover ------------------------------------------------------------
print("\nCROSSOVER  (pass vs always; negative = the pass beats blanket DIT)")
prev = None
for K in KS:
    p = paired(K, "always", "pass")
    if p is None:
        continue
    verdict = "pass WINS" if p[0] < 0 else "blanket wins"
    print(f"  K={K:<4} f_secret~{table[KS.index(K)][1]:5.1f}%   {p[0]:+7.2f}%  "
          f"{p[1]:>2}/{p[2]}   {verdict}")
    if prev is not None and (prev[1] < 0) != (p[0] < 0):
        print(f"       ^^ CROSSOVER between K={prev[0]} and K={K}")
    prev = (K, p[0])

# ---- closure --------------------------------------------------------------
print("\nCLOSURE CHECK   C_whole ~= (1-f)*C_public + f*C_secret")
print("  solving for C_secret at each K; must be consistent, and near "
      f"{C_SECRET_REF:+.2f}% (SignTransactionECDSA)")
# Solving for C_secret divides by f, so noise in C_public and C_whole is
# amplified by 1/f. At f=2.7% that is a 37x multiplier and the result is
# meaningless -- it is the arithmetic that is ill-conditioned, not necessarily
# the arms. Only points with enough secret fraction to divide by are allowed to
# vote; the rest are shown with their amplification factor and excluded.
F_MIN = 25.0
implied = []
for K, f, cpub, cwh in table:
    if cpub is None or cwh is None or f <= 0:
        continue
    fr = f / 100.0
    cs = (cwh[0] - (1 - fr) * cpub[0]) / fr
    used = f >= F_MIN
    if used:
        implied.append(cs)
    print(f"  K={K:<4} f={f:5.1f}%  C_public={cpub[0]:+6.2f}%  C_whole={cwh[0]:+6.2f}%"
          f"   -> C_secret={cs:+7.2f}%   {'' if used else f'[excluded: noise x{1/fr:.0f}]'}")
if len(implied) > 1:
    spread = max(implied) - min(implied)
    med = st.median(implied)
    print(f"\n  (voting points: f >= {F_MIN:.0f}%, where the 1/f amplification is "
          f"under {100/F_MIN:.0f}x)")
    print(f"\n  implied C_secret: median {med:+.2f}%, spread {spread:.2f} pp")
    ok = spread < 6.0 and abs(med - C_SECRET_REF) < 3.0
    print(f"  CLOSURE {'OK' if ok else 'SUSPECT'} -- "
          f"{'consistent with' if ok else 'does NOT match'} the measured "
          f"{C_SECRET_REF:+.2f}%")
    if not ok:
        print("  A wandering or far-off C_secret means the arms are not measuring")
        print("  what their names claim; do not quote the crossover until resolved.")

# ---- controls -------------------------------------------------------------
print("\nCONTROLS")
for K in KS:
    nf = paired(K, "baseline", "baseline2")
    nl = paired(K, "baseline", "null")
    if nf and nl:
        print(f"  K={K:<4} floor {nf[0]:+6.2f}% ({nf[1]}/{nf[2]})   "
              f"harness {nl[0]:+6.2f}% ({nl[1]}/{nl[2]})")
po = [float(r["probe_off"]) for r in rows if r["probe_off"]]
pn = [float(r["probe_on"]) for r in rows if r["probe_on"]]
if po and pn:
    ratio = st.median(pn) / st.median(po)
    print(f"\n  in-band lvp_chase {ratio:.2f}x  "
          f"{'PASS' if ratio > 3.0 else 'FAIL - DIT stopped taking effect, run is void'}")
