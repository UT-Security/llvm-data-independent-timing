#!/usr/bin/env python3
"""Analyse the SQLCipher cache-size sweep into the framework's headline figure.

SIGN CONVENTION: these are ROI TIMES, so a slower arm reads HIGHER and
overhead% = (arm/ref - 1)*100.

PAIRINGS, and why they are not interchangeable:
  always-on DIT = plain -> blanket   both are straight -O2 builds
  pass          = nodit -> hoist     both go through the MIR round-trip
Comparing hoist to plain would mix DIT placement with a code-layout difference
worth a few percent. That error once made a variant look 2.9% FASTER than an
unprotected build while executing 1.15M more instructions.
"""
import collections, json, math, pathlib, statistics as st, sys


def ci95(xs):
    return 1.96 * st.stdev(xs) / math.sqrt(len(xs)) if len(xs) > 1 else float("nan")


def over(ref, arm):
    """Positive => arm took LONGER."""
    return (arm / ref - 1.0) * 100.0 if ref else float("nan")


def main():
    p = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else
                     pathlib.Path.home() / "Documents/dit-browser-bench/results-sqlc.jsonl")
    rows = [json.loads(l) for l in open(p) if l.strip()]
    good = [r for r in rows if r.get("ok")]
    print(f"runs: {len(good)} good, {len(rows) - len(good)} failed")

    print("\nGATES")
    cks = {r["checksum"] for r in good}
    print(f"  checksums identical across ALL arms and cache sizes: "
          f"{'OK (' + str(cks.pop()) + ')' if len(cks) == 1 else '*** MISMATCH ' + str(cks)}")
    dit_ok = all(r["dit_on"] == (1 if r["arm"] == "blanket" else 0) for r in good)
    print(f"  DIT flag set only in the blanket arm: {'OK' if dit_ok else '*** WRONG'}")

    caches = sorted({r["cache"] for r in good})
    print(f"\n{'cache':>6} {'dec/q':>7} {'plain ms':>9} {'always-on':>12} "
          f"{'pass(hoist)':>14} {'pass vs always':>16}")
    print("-" * 74)
    table = []
    for c in caches:
        sel = [r for r in good if r["cache"] == c]
        per = {a: {r["rep"]: r["roi_ns"] for r in sel if r["arm"] == a}
               for a in ("plain", "blanket", "nodit", "hoist")}
        if not all(per.values()):
            continue
        reps = sorted(set.intersection(*(set(v) for v in per.values())))
        if not reps:
            continue
        always = [over(per["plain"][k], per["blanket"][k]) for k in reps]
        pas = [over(per["nodit"][k], per["hoist"][k]) for k in reps]
        # pass vs always-on: both are overheads on their own baseline, so the
        # comparison is the difference of the two ratios, per rep.
        vs = [((1 + pas[i] / 100) / (1 + always[i] / 100) - 1) * 100 for i in range(len(reps))]
        decq = st.median([r["decrypts_per_query"] for r in sel])
        plain_ms = st.median(list(per["plain"].values())) / 1e6
        table.append((c, decq, plain_ms, st.median(always), ci95(always),
                      st.median(pas), ci95(pas), st.median(vs), ci95(vs), len(reps)))
        print(f"{c:>6} {decq:>7.3f} {plain_ms:>9.2f} "
              f"{st.median(always):>+8.2f}%+/-{ci95(always):<4.2f} "
              f"{st.median(pas):>+10.2f}%+/-{ci95(pas):<4.2f} "
              f"{st.median(vs):>+11.2f}%+/-{ci95(vs):<4.2f}")

    print("\nREADING THE CURVE (negative 'pass vs always' = selective placement WINS)")
    wins = [t for t in table if t[7] < 0]
    losses = [t for t in table if t[7] >= 0]
    if wins:
        print(f"  pass WINS at dec/q <= {max(t[1] for t in wins):.3f} "
              f"(cache >= {min(t[0] for t in wins)})")
    if losses:
        print(f"  pass LOSES at dec/q >= {min(t[1] for t in losses):.3f} "
              f"(cache <= {max(t[0] for t in losses)})")
    if wins and losses:
        print(f"  CROSSOVER lies between dec/q {max(t[1] for t in wins):.3f} "
              f"and {min(t[1] for t in losses):.3f}")
    print("\n  Note: total work FALLS as decrypts fall (removing crypto removes work),")
    print("  so compare arms WITHIN a cache size, never ROI times across cache sizes.")


if __name__ == "__main__":
    main()
