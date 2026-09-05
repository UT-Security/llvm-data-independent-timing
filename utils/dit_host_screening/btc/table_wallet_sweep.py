#!/usr/bin/env python3
"""Emit the paper table for the Bitcoin wallet secret-fraction sweep.

NOTE ON THE LAST COLUMN. It is `pass/passnop` and it is NOT the switch cost,
though it was labelled that until 2026-09-02. `passnop` emits `HINT #0` in place
of every inserted `MSR DIT`, so PSTATE.DIT is never set and that arm loses the
PROTECTION along with the switch. The column is therefore

    switch cost  +  DIT dwell over the regions the pass protects

and the dwell term grows with the secret lane, which is most of why the column
climbs with K. Experiment 01's own gem5 flow isolates serialisation on one binary
under two switch models and gets +1.21% at K=400 against the +5.98% this column
reports.
Do not describe it as the toggle bill.

Reads the same CSV as analyze_wallet_sweep.py and emits the table in Markdown
and LaTeX. Adds what the analyser does not: an exact two-sided sign test on
every paired cell, so a median ratio is never reported as a win or a loss
without saying whether 20 paired reps can distinguish it from zero.

With N=20 the two-sided sign test rejects at p<0.05 only for n<=5 or n>=15,
so K=100's 8/20 is a tie however its median reads.

Usage: table_wallet_sweep.py [csv] [--latex]
"""
import csv, math, os, statistics as st, sys

HERE = os.path.dirname(os.path.abspath(__file__))
args = [a for a in sys.argv[1:] if not a.startswith("--")]
path = args[0] if args else os.path.join(HERE, "wallet_sweep.csv")
rows = list(csv.DictReader(open(path)))
KS = sorted({int(r["inputs"]) for r in rows})
C_SECRET_REF = 3.39


def series(K, arm):
    return {int(r["rep"]): float(r["ns_per_op"])
            for r in rows if int(r["inputs"]) == K and r["arm"] == arm}


def binom_two_sided(n, N):
    """Exact two-sided sign-test p-value for n successes in N at p=0.5."""
    pmf = [math.comb(N, k) / 2.0 ** N for k in range(N + 1)]
    return min(1.0, sum(p for p in pmf if p <= pmf[n] + 1e-12))


def paired(K, a, b):
    """median per-rep ratio b/a in percent, count slower, N, p."""
    A, B = series(K, a), series(K, b)
    reps = sorted(set(A) & set(B))
    if not reps:
        return None
    rat = [B[r] / A[r] for r in reps]
    n = sum(1 for x in rat if x > 1.0)
    return (st.median(rat) - 1) * 100, n, len(rat), binom_two_sided(n, len(rat))


def verdict(p):
    """Direction only when the sign test can actually resolve it."""
    if p is None:
        return "-"
    med, n, N, pv = p
    if pv >= 0.05:
        return "tie"
    return "pass" if med < 0 else "blanket"


def L(v):
    """Signed percent in math mode, so the minus is a real minus glyph."""
    return f"${v:+.2f}\\%$"


def tex_stars(pv):
    st_ = stars(pv)
    return r"\mathrm{ns}" if st_ == "ns" else st_


def stars(pv):
    return "***" if pv < 0.001 else "**" if pv < 0.01 else "*" if pv < 0.05 else "ns"


TAB = []
for K in KS:
    base, pub = series(K, "baseline"), series(K, "pub_base")
    reps = sorted(set(base) & set(pub))
    f = st.median([(base[r] - pub[r]) / base[r] for r in reps]) * 100
    TAB.append(dict(
        K=K, f=f, base_ms=st.median(base.values()) / 1e6,
        cpub=paired(K, "pub_base", "pub_always"),
        cwh=paired(K, "baseline", "always"),
        vpass=paired(K, "baseline", "pass"),
        xover=paired(K, "always", "pass"),
        sw=paired(K, "passnop", "pass"),
        floor=paired(K, "baseline", "baseline2"),
        null=paired(K, "baseline", "null"),
    ))

N = TAB[0]["xover"][2]
probe = (st.median([float(r["probe_on"]) for r in rows if r["probe_on"]]) /
         st.median([float(r["probe_off"]) for r in rows if r["probe_off"]]))

# ---- implied C_secret (closure), voting points only -----------------------
F_MIN = 25.0
implied = [(t["K"], (t["cwh"][0] - (1 - t["f"] / 100) * t["cpub"][0]) / (t["f"] / 100))
           for t in TAB]
voting = [c for K, c in implied if dict((t["K"], t["f"]) for t in TAB)[K] >= F_MIN]
med_cs, spread = st.median(voting), max(voting) - min(voting)

if "--latex" not in sys.argv:
    print(f"Table 1: Selective vs blanket DIT across the secret fraction of one "
          f"Bitcoin Core wallet call\n")
    print(f"WalletCreateTxUsePresetInputsAndCoinSelection, Apple silicon, "
          f"{N} paired reps per cell, rotated arm order.")
    print(f"Median per-rep ratio; n/{N} = reps where the first-named arm is slower; "
          f"exact two-sided sign test.\n")
    print("| K (inputs) | f_secret | baseline | C_public | C_whole | pass vs base | "
          "pass vs blanket | sign test | verdict | pass vs nop |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for t in TAB:
        x = t["xover"]
        print(f"| {t['K']} | {t['f']:.1f}% | {t['base_ms']:.2f} ms | "
              f"{t['cpub'][0]:+.2f}% | {t['cwh'][0]:+.2f}% | {t['vpass'][0]:+.2f}% | "
              f"**{x[0]:+.2f}%** | {x[1]}/{x[2]} {stars(x[3])} | "
              f"{verdict(x)} | {t['sw'][0]:+.2f}% |")
    print(f"\nControls (all arms, every K):")
    fl = [t["floor"][0] for t in TAB]; nu = [t["null"][0] for t in TAB]
    print(f"  noise floor (baseline2/baseline)  {min(fl):+.2f}% .. {max(fl):+.2f}%")
    print(f"  harness null (DIT never written)  {min(nu):+.2f}% .. {max(nu):+.2f}%")
    print(f"  in-band lvp_chase positive control {probe:.2f}x "
          f"({'PASS' if probe > 3.0 else 'FAIL'})")
    print(f"\nClosure  C_whole ~= (1-f)*C_public + f*C_secret")
    print(f"  implied C_secret (f >= {F_MIN:.0f}%): median {med_cs:+.2f}%, "
          f"spread {spread:.2f} pp, reference {C_SECRET_REF:+.2f}% -> "
          f"{'OK' if spread < 6 and abs(med_cs - C_SECRET_REF) < 3 else 'SUSPECT'}")
else:
    print(r"\begin{table}[t]\centering\small")
    print(r"\caption{Selective vs.\ blanket \textsc{dit} across the secret fraction of a"
          r" single Bitcoin Core wallet call (\texttt{CreateTransaction}). Median of "
          + str(N) + r" paired repetitions, rotated arm order; $n/" + str(N) +
          r"$ counts repetitions in which the pass is slower than blanket, with an exact"
          r" two-sided sign test. The pass wins where the secret fraction is small and"
          r" loses where it is large; at $f\approx45\%$ the two are indistinguishable.}")
    print(r"\label{tab:btc-fsweep}")
    print(r"\begin{tabular}{rrrrrrrrl}\toprule")
    print(r"$K$ & $f_{\mathrm{secret}}$ & base & $C_{\mathrm{public}}$ & "
          r"$C_{\mathrm{whole}}$ & pass & \multicolumn{2}{c}{pass vs.\ blanket} & "
          r"verdict\\")
    print(r"(inputs) & & (ms) & & & vs.\ base & ratio & $n/" + str(N) + r"$ & \\\midrule")
    for t in TAB:
        x = t["xover"]
        v = {"pass": r"\textbf{pass}", "blanket": "blanket", "tie": "tie"}[verdict(x)]
        print(f"{t['K']} & {t['f']:.1f}\\% & {t['base_ms']:.2f} & "
              f"{L(t['cpub'][0])} & {L(t['cwh'][0])} & {L(t['vpass'][0])} & "
              f"{L(x[0])} & {x[1]}/{x[2]}$^{{{tex_stars(x[3])}}}$ & {v}\\\\")
    print(r"\bottomrule\end{tabular}")
    print(r"\end{table}")
