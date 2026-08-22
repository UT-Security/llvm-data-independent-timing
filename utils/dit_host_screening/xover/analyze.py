#!/usr/bin/env python3
"""Turn a crossover sweep into the table the evaluation section needs.

THE MODEL BEING TESTED. With f the secret fraction, c_P and c_S the always-on DIT
cost of the public and secret lanes measured separately, tau the pass's placement
overhead as a fraction of the secret region, and phi the cost of false positives
as a fraction of the public region:

    overhead(blanket) = c_P(1-f) + c_S f
    overhead(pass)    = c_S f + tau f + phi(1-f)
    win(pass)         = (c_P - phi)(1-f) - tau f
    f*                = (c_P - phi) / (c_P - phi + tau)

c_S f cancels, which is why the secret lane's own DIT cost does not decide the
comparison. Below f* selective wins; above it, blanket does.

REPORTING RULES (van der Kouwe et al., "SoK: Benchmarking Flaws in Systems
Security", EuroS&P'19):
  * geometric mean over ratios, never arithmetic (flaw B5)
  * variation reported, never a bare central value (flaw B4)
  * per-point rows, not only an aggregate (flaw F3)
  * negative overheads explained, not quietly reported (they should be
    impossible here - DIT can only remove optimizations)
"""
import argparse, json, math, pathlib, statistics, sys
from collections import defaultdict


def geomean(xs):
    xs = [x for x in xs if x > 0]
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else float("nan")


def ci95(xs):
    if len(xs) < 2:
        return 0.0
    return 1.96 * statistics.stdev(xs) / math.sqrt(len(xs))


def load(p):
    return [json.loads(l) for l in open(p) if l.strip()]


# --------------------------------------------------------------- gem5 ------
def analyze_gem5(recs, args):
    by = defaultdict(dict)
    for r in recs:
        if r.get("cycles"):
            by[(r["cfg"], r["sigs"], r["period"], r["verifies"])][r["arm"]] = r

    # f is measured, never inferred from the knob: difference the `off` arm at
    # this point against the `off` arm with sigs=0 in the same config. Exact,
    # because gem5 cycle counts are deterministic.
    zero = {}
    for (cfg, s, p, v), arms in by.items():
        if s == 0 and "off" in arms:
            zero[(cfg, v)] = arms["off"]["cycles"]

    # Build rows first so they can be ordered by MEASURED f rather than by knob.
    rows = []
    for key in by:
        cfg, s, p, v = key
        arms = by[key]
        if "off" not in arms:
            continue
        base = arms["off"]["cycles"]
        z = zero.get((cfg, v))
        f = 100.0 * (base - z) / base if z and base > z else (0.0 if s == 0 else float("nan"))

        def ov(a):
            return 100.0 * (arms[a]["cycles"] / base - 1) if a in arms else None

        cells = {a: ov(a) for a in ("always", "oracle", "hoist", "gated", "hoist0", "nopctl")}
        al = cells.get("always")
        # THE PRIZE: what a perfect placement would save. Upper bound on any pass.
        oracle_d = (cells["oracle"] - al) if (cells.get("oracle") is not None and al is not None) else None
        # WHAT THE COMPILER ACTUALLY COLLECTS. The oracle is deliberately EXCLUDED
        # here - including it scores a point "selective wins" when the pass in fact
        # loses and only the unreachable upper bound wins.
        pa = [(a, cells[a]) for a in ("hoist", "gated", "hoist0") if cells.get(a) is not None]
        best = min(pa, key=lambda t: t[1]) if pa else (None, None)
        pass_d = (best[1] - al) if (best[1] is not None and al is not None) else None
        rows.append({"cfg": cfg, "sigs": s, "period": p, "verifies": v, "f": f,
                     "cells": cells, "oracle_d": oracle_d, "pass_d": pass_d,
                     "best_arm": best[0]})

    rows.sort(key=lambda r: (r["cfg"], r["verifies"],
                             r["f"] if not math.isnan(r["f"]) else 1e9))

    print(f"\n{'='*122}")
    print(f"{'cfg':<8}{'sigs':>5}{'per':>5}{'ver':>4}{'f':>8}  "
          f"{'always':>9}{'oracle':>9}{'hoist':>9}{'gated':>9}{'hoist0':>9}"
          f"{'prize':>10}{'pass':>10}   verdict")
    print(f"{'='*122}")
    fmt = lambda x: f"{x:+.2f}%" if x is not None and not (isinstance(x, float) and math.isnan(x)) else "-"
    for r in rows:
        verdict = ""
        if r["pass_d"] is not None:
            verdict = "SELECTIVE" if r["pass_d"] < 0 else "blanket"
        fs = f"{r['f']:.2f}%" if not math.isnan(r["f"]) else "  n/a"
        print(f"{r['cfg']:<8}{r['sigs']:>5}{r['period']:>5}{r['verifies']:>4}{fs:>8}  "
              + "".join(f"{fmt(r['cells'][a]):>9}" for a in ("always", "oracle", "hoist", "gated", "hoist0"))
              + f"{fmt(r['oracle_d']):>10}{fmt(r['pass_d']):>10}   {verdict}")

    # ---- crossover location, per config ----
    def crossing(pts, field):
        pts = sorted([r for r in pts if r[field] is not None and not math.isnan(r["f"])
                      and r["f"] > 0], key=lambda r: r["f"])
        for a, b in zip(pts, pts[1:]):
            if a[field] < 0 <= b[field]:
                t = -a[field] / (b[field] - a[field])
                return a["f"] * (b["f"] / a["f"]) ** t, pts
        return None, pts

    print(f"\n{'-'*122}")
    print("CROSSOVER f*  -  ORACLE is the prize boundary (is there anything to win),")
    print("                 PASS is what this compiler actually collects.")
    print(f"{'-'*122}")
    for cfg in sorted({r["cfg"] for r in rows}):
        sub = [r for r in rows if r["cfg"] == cfg]
        for field, label in (("oracle_d", "oracle"), ("pass_d", "pass  ")):
            cross, pts = crossing(sub, field)
            wins = sum(1 for r in pts if r[field] < 0)
            if cross:
                print(f"  {cfg:<8} {label}  f* = {cross:6.2f}%   (wins {wins}/{len(pts)} points)")
            elif pts:
                allwin = all(r[field] < 0 for r in pts)
                bound = pts[-1]["f"] if allwin else pts[0]["f"]
                print(f"  {cfg:<8} {label}  f* {'>' if allwin else '<'} {bound:6.2f}%  "
                      f"(wins {wins}/{len(pts)} points, no crossing in range)")

    # ---- coverage gate ----
    print(f"\n{'-'*104}\nCOVERAGE (ditSuppressed as % of oracle - below 100% means UNDER-PROTECTION)\n{'-'*104}")
    for key in sorted(by, key=lambda k: (k[0], k[3], -k[2], k[1])):
        arms = by[key]
        if "oracle" not in arms or not arms["oracle"].get("ditSuppressed"):
            continue
        o = arms["oracle"]["ditSuppressed"]
        cells = []
        for a in ("always", "hoist", "gated", "hoist0"):
            if a in arms and arms[a].get("ditSuppressed") is not None:
                cells.append(f"{a}={100*arms[a]['ditSuppressed']/o:6.1f}%")
        offsup = arms.get("off", {}).get("ditSuppressed")
        flag = "" if not offsup else f"  !! off={offsup} SHOULD BE 0"
        print(f"  {key[0]:<8} s={key[1]:<3} p={key[2]:<5} oracle={o:>12,.0f}  "
              + "  ".join(cells) + flag)

    # ---- switch-model comparison: the renamed-switch counterfactual ----
    print(f"\n{'-'*104}\nSWITCH MODEL (serializing -> renamed; renaming must never make a run SLOWER)\n{'-'*104}")
    for s, p, v in sorted({(r["sigs"], r["period"], r["verifies"]) for r in rows}):
        ser = next((r for r in rows if r["cfg"] == "serdit" and (r["sigs"], r["period"], r["verifies"]) == (s, p, v)), None)
        spc = next((r for r in rows if r["cfg"] == "spec" and (r["sigs"], r["period"], r["verifies"]) == (s, p, v)), None)
        if not ser or not spc:
            continue
        line = []
        for a in ("always", "oracle", "hoist", "gated", "hoist0"):
            if ser["cells"].get(a) is not None and spc["cells"].get(a) is not None:
                line.append(f"{a} {ser['cells'][a]:+.2f}->{spc['cells'][a]:+.2f}")
        print(f"  f={ser['f']:5.2f}%  " + "  ".join(line))

    return rows


def analyze_opt(recs):
    """The forward-looking sweep: c_P rises as more optimizations are DIT-gated,
    so f* moves right. gem5 is the only instrument that can run this."""
    by = defaultdict(dict)
    for r in recs:
        if r.get("cycles"):
            by[(r["cfg"], r["sigs"], r["period"])][r["arm"]] = r
    order = ["opt0", "opt1", "opt2", "opt3"]
    label = {"opt0": "none (VP off)", "opt1": "+EVES", "opt2": "+EVES+DMP",
             "opt3": "+EVES+DMP+CompSimp"}
    print(f"\n{'='*96}\nOPTIMIZATION-COUNT SWEEP - what always-on costs as hardware gates more\n{'='*96}")
    print(f"{'gated optimizations':<24}{'point':>10}{'always (c_P proxy)':>20}"
          f"{'oracle':>10}{'gated':>10}{'selective advantage':>22}")
    for (cfg, s, p) in sorted(by, key=lambda k: (k[1], k[2], order.index(k[0]) if k[0] in order else 9)):
        arms = by[(cfg, s, p)]
        if "off" not in arms or cfg not in order:
            continue
        base = arms["off"]["cycles"]
        ov = lambda a: 100.0 * (arms[a]["cycles"] / base - 1) if a in arms else None
        al, o, g = ov("always"), ov("oracle"), ov("gated")
        adv = (min(x for x in (o, g) if x is not None) - al) if al is not None and (o is not None or g is not None) else None
        f = lambda x: f"{x:+.2f}%" if x is not None else "-"
        print(f"{label.get(cfg, cfg):<24}{f's{s}/p{p}':>10}{f(al):>20}{f(o):>10}"
              f"{f(g):>10}{f(adv):>22}")


# ------------------------------------------------------------- native ------
def analyze_native(recs):
    by = defaultdict(lambda: defaultdict(list))
    for r in recs:
        by[(r["sigs"], r["period"], r["verifies"])][r["arm"]].append(r)

    print(f"\n{'='*112}")
    print(f"{'sigs':>5}{'per':>5}{'ver':>4}{'f meas':>9}{'base ms':>10}  "
          f"{'always':>16}{'oracle':>16}{'hoist':>16}{'gated':>16}{'hoist0':>16}")
    print(f"{'='*112}")
    rows = []
    for key in sorted(by, key=lambda k: (-k[1], k[0], k[2])):
        arms = by[key]
        if "off" not in arms:
            continue
        base = statistics.median([x["total_s"] for x in arms["off"]])
        f = statistics.median([x["f"] for x in arms["off"]])
        cells = {}
        for a in ("always", "oracle", "hoist", "gated", "hoist0", "off2"):
            if a not in arms:
                continue
            # pair by rep so drift cancels
            per_rep = defaultdict(dict)
            for x in arms[a]:
                per_rep[x["rep"]]["a"] = x["total_s"]
            for x in arms["off"]:
                per_rep[x["rep"]]["b"] = x["total_s"]
            ratios = [v["a"] / v["b"] for v in per_rep.values() if "a" in v and "b" in v]
            if not ratios:
                continue
            g = geomean(ratios)
            cells[a] = (100 * (g - 1), 100 * ci95(ratios),
                        sum(1 for x in ratios if x > 1.0), len(ratios))
        fmt = lambda a: (f"{cells[a][0]:+6.2f}%+-{cells[a][1]:.2f} {cells[a][2]}/{cells[a][3]}"
                         if a in cells else "-")
        print(f"{key[0]:>5}{key[1]:>5}{key[2]:>4}{f:>8.3f}%{base*1000:>10.1f}  "
              + "".join(f"{fmt(a):>16}" for a in ("always", "oracle", "hoist", "gated", "hoist0")))
        rows.append({"key": key, "f": f, "cells": cells})

    floor = [r["cells"]["off2"][0] for r in rows if "off2" in r["cells"]]
    if floor:
        print(f"\nnoise floor (off2 vs off): geomean {geomean([1+x/100 for x in floor])*100-100:+.3f}%  "
              f"range {min(floor):+.2f}%..{max(floor):+.2f}%")
        print("Any effect smaller than this floor is NOT resolvable on this rig.")

    print(f"\n{'-'*112}\nSELECTIVE MINUS BLANKET (negative = selective wins)\n{'-'*112}")
    for r in rows:
        if "always" not in r["cells"]:
            continue
        al = r["cells"]["always"][0]
        line = []
        for a in ("oracle", "hoist", "gated", "hoist0"):
            if a in r["cells"]:
                line.append(f"{a} {r['cells'][a][0]-al:+6.2f}")
        print(f"  f={r['f']:6.3f}%  always={al:+6.2f}%   " + "  ".join(line))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--kind", choices=["gem5", "native", "opt"], default="gem5")
    a = ap.parse_args()
    recs = load(a.path)
    if not recs:
        sys.exit("no records")
    if a.kind == "native":
        analyze_native(recs)
    elif a.kind == "opt":
        analyze_opt(recs)
    else:
        analyze_gem5(recs, a)


if __name__ == "__main__":
    main()
