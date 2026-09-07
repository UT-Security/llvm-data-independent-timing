#!/usr/bin/env python3
"""Experiment 14: combine several runs of the driver into one JSON with the driver's schema.

  aggregate_awslc.py OUT.json run-1/speed.json run-2/speed.json ...

Per cell (row x arm) the aggregate's headline value is, by default, the arithmetic MEAN across runs
of each run's median (cycles/op, instructions/op, ns/op); AGGREGATE=median uses the MEDIAN across
runs instead, which a single corrupted run cannot move (the counter fault corrupts one run's median
in about one cell in fifty). Both are always stored per cell (mean_across_runs, median_across_runs);
analyze_awslc.py reads the headline unchanged. Nothing is dropped either way. It also carries,
per cell, the per-run values and the spread across runs ((max - min) / mean, as percent), and
at the top level the per-run validity records. Nothing is dropped: a cell marked suspect in
any run stays marked, and the run count that marked it is kept; the flagged clocks of every run
are concatenated. A row missing from some run is averaged over the runs that have it.
"""
import json, os, sys, statistics as st

def main():
    out, paths = sys.argv[1], sys.argv[2:]
    how = os.environ.get('AGGREGATE', 'mean').lower()
    if how not in ('mean', 'median'): raise SystemExit("AGGREGATE must be mean or median")
    if not paths: raise SystemExit(__doc__)
    runs = [json.load(open(p)) for p in paths]
    arms = runs[0].get('arms') or ['A', 'C', 'B', 'Bs', 'H', 'Hs']
    keys = sorted({k for d in runs for k in d['results']})
    agg = {}
    for k in keys:
        per = [d['results'][k] for d in runs if k in d['results']]
        cyc = {a: [r['median_cycles_per_op'][a] for r in per if a in r['median_cycles_per_op']] for a in arms}
        cyc = {a: v for a, v in cyc.items() if v}
        ins = {a: [r['median_instrs_per_op'][a] for r in per if a in r.get('median_instrs_per_op', {})] for a in arms}
        ins = {a: v for a, v in ins.items() if v}
        nsop = {a: [r['median_ns_per_op'][a] for r in per if a in r.get('median_ns_per_op', {})] for a in arms}
        nsop = {a: v for a, v in nsop.items() if v}
        mean = lambda v: sum(v) / len(v)
        pick = mean if how == 'mean' else st.median
        m = {a: pick(v) for a, v in cyc.items()}
        i = {a: pick(v) for a, v in ins.items()}
        n = {a: pick(v) for a, v in nsop.items()}
        mean_c = {a: mean(v) for a, v in cyc.items()}; median_c = {a: st.median(v) for a, v in cyc.items()}
        spread = {a: ((max(v) - min(v)) / mean(v) * 100 if len(v) > 1 else 0.0) for a, v in cyc.items()}
        suspect = {}
        for r in per:
            sus = r.get('suspect_cells')
            if sus is None:   # JSON from a driver that predates the marking: the shape a corrupted median takes
                mm = r['median_cycles_per_op']
                sus = [a for a in mm if a != 'A' and (mm[a] > 20 * mm['A'] or mm[a] < mm['A'] / 20)]
                if r.get('ns_per_op_A') and not 3000 <= mm['A'] / r['ns_per_op_A'] * 1000 <= 4700: sus.append('A')
            for a in sus: suspect[a] = suspect.get(a, 0) + 1
        ipc = {a: i[a] / m[a] for a in m if a in i}
        clock = {a: m[a] / n[a] * 1000 for a in m if a in n}
        # a cell is SUSPECT when the combined value itself implies a clock outside the P-core band. With the
        # mean, one corrupted run is enough, so any run's mark carries over; with the median the value is
        # clean unless a majority of runs was corrupted, and the per-run marks stay in suspect_runs as a note.
        value_bad = [a for a in clock if not 3000 <= clock[a] <= 4700]
        marked = sorted(set(value_bad) | (set(suspect) if how == 'mean' else set()))
        entry = dict(
            median_cycles_per_op=m, median_instrs_per_op=i, median_ns_per_op=n,
            cell_clock_mhz=clock,
            per_run_cycles_per_op=cyc, mean_across_runs=mean_c, median_across_runs=median_c, aggregate=how,
            run_spread_pct=spread, runs_with_row=len(per),
            suspect_cells=marked, suspect_runs=suspect,
            ipc=ipc, ipc_A=ipc.get('A'), mad_pct=mean([r['mad_pct'] for r in per]),
            pct_vs_A={a: (m[a] / m['A'] - 1) * 100 for a in m if a != 'A'},
            abs_bracket_cycles=(m['B'] - m['A']) if 'B' in m else None,
            abs_bracket_instrs=(i['B'] - i['A']) if 'B' in i and 'A' in i else None,
            ns_per_op_A=n.get('A'), n=sum(r.get('n', 0) for r in per))
        if 'Bs' in m and 'B' in m: entry['Bs_minus_B_pts'] = (m['Bs'] - m['B']) / m['A'] * 100
        if 'B' in m and 'H' in m: entry['B_minus_H_pts'] = (m['B'] - m['H']) / m['A'] * 100
        agg[k] = entry
    gate = sorted({tuple(g) for d in runs for g in d.get('gate', [])})
    top = dict(results=agg, runs_count=len(runs), aggregate=how, runs=[dict(source=p, flagged=d.get('flagged'), unpinned=d.get('unpinned'),
                                                             failures=len(d.get('failures', [])), reps=d.get('reps'), timeout_ms=d.get('timeout_ms'),
                                                             no_pmc_rows=d.get('no_pmc_rows'), rows=len(d['results'])) for p, d in zip(paths, runs)],
               gate=[list(g) for g in gate], failures=[f for d in runs for f in d.get('failures', [])],
               flagged=sum(d.get('flagged', 0) or 0 for d in runs), flagged_clocks=[c for d in runs for c in d.get('flagged_clocks', [])],
               all_clocks_mhz=[c for d in runs for c in d.get('all_clocks_mhz', [])], clock_band=runs[0].get('clock_band'),
               no_pmc_rows=sum(d.get('no_pmc_rows', 0) or 0 for d in runs), unpinned=sum(d.get('unpinned', 0) or 0 for d in runs),
               pin_cpu=runs[0].get('pin_cpu'), arms=arms, tests=runs[0].get('tests'), reps=runs[0].get('reps'),
               timeout_ms=runs[0].get('timeout_ms'), chunks=runs[0].get('chunks'))
    json.dump(top, open(out, 'w'), indent=1)
    sp = sorted(v for e in agg.values() for v in e['run_spread_pct'].values())
    print(f"aggregated {len(runs)} runs ({how} across runs), {len(agg)} rows; per-cell spread across runs (max-min)/mean: median {st.median(sp):.2f}%, p90 {sp[int(len(sp)*0.9)]:.2f}%, max {sp[-1]:.1f}%; "
          f"suspect cells in the combined table: {sum(len(e['suspect_cells']) for e in agg.values())} (some run corrupted a cell in {sum(1 for e in agg.values() if e['suspect_runs'])} rows); flagged samples total {top['flagged']}; unpinned {top['unpinned']}")

if __name__ == '__main__':
    main()
