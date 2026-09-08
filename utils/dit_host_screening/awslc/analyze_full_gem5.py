#!/usr/bin/env python3
"""Experiment 14 on gem5: digest a full-suite sweep.

Hundreds of rows x (arm, model) pairs is thousands of measurements, so the per-row table the
paper-filter runs print is useless here.

WHAT THIS REPORTS, AND WHY IN THIS ORDER

Every number below is a ratio of two gem5 runs, and the two runs differ in more than one
thing unless you are careful. Two of the comparisons available here differ in EXACTLY one
thing, and they are reported first because they are the only ones that need no caveat:

  DIT MODE          blanket vs unhardened.  Arm C is the SAME BINARY as arm A -- the
                    constructor in blanket_ctor.c is linked into every build and gated on
                    AWSLC_BLANKET, so the two arms are byte-for-byte identical code at
                    identical addresses and differ only in whether one branch is taken
                    before main.  No instructions added, nothing moved.  The difference is
                    the price of running with PSTATE.DIT set: the value predictor and the
                    other data-dependent optimisations are suppressed.  This is what DIT
                    ITSELF costs.

  BLANKET IS ONE ARM, NOT ONE PER MODEL.  Arm C commits no `msr DIT` inside any ROI -- its
                    single write is in a constructor before main -- so there is no mode
                    switch there to measure and no reason to carry two columns of it.  The
                    blanket number is taken from the APPLE run and used as the one blanket
                    arm.  (It is not bit-identical across the two models: under ExpeDITe the
                    DIT register is renamed, so every gated instruction sources it and
                    scheduling shifts.  Measured at median -0.001%, range -1.50% to +2.41%
                    over the suite -- second-order and bidirectional, and NOT switch cost,
                    which is exactly why it is not given a column of its own.)

  SWITCH MECHANISM  Apple vs ExpeDITe, same arm.  The identical binary runs under both
                    models with the identical instruction stream and the identical dwell;
                    only the microarchitectural handling of `msr DIT` differs.  This is
                    what the SWITCH DESIGN costs, and it is the counterfactual silicon
                    cannot provide.

Everything after those crosses `ditisb` against `rel`, which are different binaries, so it
carries a LAYOUT TERM.  That term is not assumed -- it is measured here, from the rows that
run the `ditisb` binary but never enter the bracket and therefore commit no DIT at all.  On
this suite it is around 3% in absolute value, which is larger than several of the effects it
would sit inside, so those comparisons are printed with the term beside them and should not
be quoted at the median without it.

usage: analyze_full_gem5.py <out dir> [<out dir> ...] [--top N]
       (pass both the main sweep and the blanket-arm sweep to get the full decomposition)
"""
import argparse, csv, collections, statistics, os, sys

# The arms, spelled out.  Never print the single-letter keys.
ARM = {"A": "unhardened", "C": "blanket", "B": "shipped bracket", "H": "hoisted"}


def load(dirs):
    rows = collections.defaultdict(lambda: collections.defaultdict(dict))
    for d in dirs:
        p = os.path.join(d, "results.csv")
        if not os.path.exists(p):
            print(f"note: {p} not found, skipping", file=sys.stderr)
            continue
        for r in csv.DictReader(open(p)):
            rows[r["cfg"]][r["row"]][r["arm"]] = r
    return rows


def cyc(v, arm):
    try:
        return float(v[arm]["cycles_per_op"])
    except (KeyError, TypeError, ValueError):
        return None


def delta(v, arm, base):
    a, b = cyc(v, base), cyc(v, arm)
    return (b / a - 1) * 100 if a and b else None


def spread(xs):
    xs = sorted(x for x in xs if x is not None)
    if not xs:
        return None
    return (statistics.median(xs), xs[int(len(xs) * 0.9)], max(xs), len(xs))


def band(label, xs, note=""):
    s = spread(xs)
    if s:
        print(f"{label:<56s}{s[0]:>+9.2f}%{s[1]:>+9.2f}%{s[2]:>+9.1f}%   {note}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", nargs="+")
    ap.add_argument("--top", type=int, default=20)
    a = ap.parse_args()
    d = load(a.out)
    if not d:
        sys.exit("no results")
    cfgs = list(d)
    ref = cfgs[0]

    # ---- coverage -------------------------------------------------------------------
    ent = {k: float(v["B"]["dit_read_per_op"]) for k, v in d[ref].items() if "B" in v}
    covered = {k for k, e in ent.items() if e > 0}
    print(f"=== coverage: {len(covered)} of {len(ent)} rows enter a bracketed entry point ===")
    byent = collections.Counter()
    for k, e in ent.items():
        byent["0" if e == 0 else "1" if e <= 1 else "2-9" if e < 10
              else "10-99" if e < 100 else "100+"] += 1
    for b in ("0", "1", "2-9", "10-99", "100+"):
        if byent[b]:
            print(f"  {b:>6s} entries/op   {byent[b]:>4d} rows")
    deep = sorted(((e, k) for k, e in ent.items() if e >= 10), reverse=True)[:8]
    if deep:
        print("  deepest nesting:")
        for e, k in deep:
            print(f"    {e:>9,.0f}  {k[:56]}")

    # ---- the layout term, measured --------------------------------------------------
    # Rows that run the `ditisb` binary but never enter the bracket: no `msr DIT` commits and
    # dwell is zero, so `ditisb` vs `rel` here is the binary difference and nothing else.
    lay = []
    for k, v in d[ref].items():
        if ent.get(k, 1) != 0 or "B" not in v or "A" not in v:
            continue
        if float(v["B"].get("dit_dwell_frac") or 0):
            continue
        x = delta(v, "B", "A")
        if x is not None:
            lay.append(x)
    layout_note = ""
    if lay:
        mabs = statistics.mean(abs(x) for x in lay)
        layout_note = f"layout, mean |.| {mabs:.2f}%"
        print(f"\n=== layout control: `ditisb` vs `rel` on the {len(lay)} rows that run the "
              f"hardened binary\n    but never enter the bracket (no DIT commits, zero dwell) ===")
        print(f"  median {statistics.median(lay):+.2f}%   min {min(lay):+.2f}%   "
              f"max {max(lay):+.2f}%   mean |.| {mabs:.2f}%")
        print("  Any comparison below that crosses the two binaries carries this term.")

    # ---- the arms, as reported -------------------------------------------------------
    # Three arms against the unhardened reference.  Blanket is drawn once, from the Apple
    # run, because it switches the mode nowhere inside an ROI.
    blank_cfg = "apple" if "apple" in cfgs else cfgs[0]
    print(f"\n=== the three arms vs unhardened  (blanket taken from the {blank_cfg} run) ===")
    print(f"{'':56s}{'median':>10s}{'p90':>10s}{'max':>10s}   confound")
    xs = [delta(v, "C", "A") for k, v in d[blank_cfg].items() if ent.get(k, 0) > 0 and "C" in v]
    band("blanket (DIT on for the whole run)", xs, "NONE - same binary as unhardened")
    for cfg in cfgs:
        rows = [v for k, v in d[cfg].items() if ent.get(k, 0) > 0]
        band(f"shipped bracket, {cfg}", [delta(v, "B", "A") for v in rows],
             layout_note or "layout term not measured")
        band(f"hoisted, {cfg}", [delta(v, "H", "A") for v in rows],
             layout_note or "layout term not measured")

    # ---- the two confound-free measurements -----------------------------------------
    print(f"\n=== confound-free: both sides are the same binary ===")
    print(f"{'':56s}{'median':>10s}{'p90':>10s}{'max':>10s}   confound")
    xs = [delta(v, "C", "A") for k, v in d[blank_cfg].items() if ent.get(k, 0) > 0 and "C" in v]
    if xs:
        band("DIT mode: blanket vs unhardened", xs, "NONE - same binary, one ctor branch")
    if len(cfgs) > 1:
        # Reference is the renamed design when it is present, so a serialising model shows a
        # POSITIVE cost against it and reads the same direction as every other row here.
        ref_m = "expedite" if "expedite" in cfgs else cfgs[0]
        for other in [c for c in cfgs if c != ref_m]:
            for arm in ("B", "H"):
                xs = []
                for k, v in d[ref_m].items():
                    if ent.get(k, 0) <= 0 or k not in d[other]:
                        continue
                    p, q = cyc(v, arm), cyc(d[other][k], arm)
                    if p and q:
                        xs.append((q / p - 1) * 100)
                band(f"switch mechanism: {other} vs {ref_m}, {ARM[arm]}", xs,
                     "NONE - same binary, same dwell")

    # ---- decomposition, layout term attached ----------------------------------------
    print(f"\n=== decomposition (crosses the two binaries; read with the layout term) ===")
    print(f"{'':56s}{'median':>10s}{'p90':>10s}{'max':>10s}   confound")
    for cfg in cfgs:
        rows = [v for k, v in d[cfg].items() if ent.get(k, 0) > 0]
        band(f"{cfg}: shipped bracket vs unhardened", [delta(v, "B", "A") for v in rows],
             layout_note or "layout term not measured")
        xs = []
        for k, v in d[cfg].items():
            if ent.get(k, 0) <= 0:
                continue
            base = cyc(d[blank_cfg].get(k, {}), "C")
            q = cyc(v, "B")
            if base and q:
                xs.append((q / base - 1) * 100)
        band(f"{cfg}: shipped bracket vs blanket", xs,
             layout_note or "layout term not measured")
        band(f"{cfg}: hoisted vs unhardened", [delta(v, "H", "A") for v in rows],
             layout_note or "layout term not measured")

    # ---- cost, ranked ---------------------------------------------------------------
    for cfg in cfgs:
        rank = []
        for k, v in d[cfg].items():
            p = delta(v, "B", "A")
            if p is not None and ent.get(k, 0) > 0:
                bl = None
                bv, ba = cyc(d[blank_cfg].get(k, {}), "C"), cyc(d[blank_cfg].get(k, {}), "A")
                if bv and ba:
                    bl = (bv / ba - 1) * 100
                rank.append((p, k, cyc(v, "A"), ent.get(k, 0), bl))
        rank.sort(reverse=True)
        if not rank:
            continue
        print(f"\n=== {cfg}: worst {a.top} rows, shipped bracket vs unhardened ===")
        print(f"{'bracket':>10s}{'DIT mode':>10s}{'unhardened':>12s}{'entries':>9s}  row")
        print(f"{'vs unhard.':>10s}{'alone':>10s}{'cyc/op':>12s}{'per op':>9s}")
        for p, k, acyc, e, c in rank[:a.top]:
            cs = f"{c:>+9.1f}%" if c is not None else f"{'-':>10s}"
            print(f"{p:>+9.1f}%{cs}{acyc:>12,.0f}{e:>9.1f}  {k[:46]}")

    # ---- unit price -----------------------------------------------------------------
    for cfg in cfgs:
        px = []
        for k, v in d[cfg].items():
            e = ent.get(k, 0)
            if e <= 0:
                continue
            p = cyc(d[blank_cfg].get(k, {}), "C") or cyc(v, "A")
            q = cyc(v, "B")
            if p and q and q > p:
                px.append((q - p) / e)
        if px:
            s = sorted(px)
            base_nm = "blanket" if any("C" in v for v in d[blank_cfg].values()) else "unhardened"
            print(f"\n{cfg}: cycles per bracket entry over {len(px)} rows, against {base_nm} -- "
                  f"p10 {s[len(s)//10]:.0f}, median {statistics.median(s):.0f}, "
                  f"p90 {s[-max(1,len(s)//10)]:.0f}")


if __name__ == "__main__":
    main()
