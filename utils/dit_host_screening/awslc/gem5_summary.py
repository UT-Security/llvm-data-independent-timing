#!/usr/bin/env python3
"""Experiment 14 on gem5: turn a sweep's cells into the analysis summary the M4 side writes,
so `latex_table_awslc.py` renders the SAME table from gem5 numbers.

The M4 rig's arms are C/B/Bs/H/Hs -- Coarse, AWS default and AWS hoist, each with and without
the `sb` after the switch.  gem5 has no `sb` axis: there is no FEAT_SB, one hardened build
(`ditisb`) serves both switch models, and under ExpeDITe the trailing `isb` is fused away at
rename.  What gem5 has instead is the SWITCH MODEL, which silicon cannot vary.  So the two
sub-columns here are the two models rather than the barrier:

    Baseline / bracket    Baseline / hoisted    ExpeDITe / bracket    ExpeDITe / hoisted

Ratios are to the Coarse arm, exactly as on the M4 side, so a cell is what the bracket's mode
switches cost over running the whole process under DIT.  Coarse is taken ONCE, from the
baseline-model run: arm C commits no `msr DIT` inside any ROI, so there is no switch there to
model and no reason to carry two columns of it.

usage: gem5_summary.py <sweep dir> [<sweep dir> ...] --out summary.json
"""
import argparse, json, os, re, sys, statistics

ROW = re.compile(r'\{"description":.*?\}', re.S)
# gem5 cell arm/model -> the summary arm key the table generator reads
MAP = {("C", "apple"): "C",
       ("B", "apple"): "B",  ("H", "apple"): "H",
       ("B", "expedite"): "Be", ("H", "expedite"): "He"}


def dumps(path):
    out, cur, inside = [], {}, False
    for line in open(path, errors="replace"):
        if line.startswith("---------- Begin Simulation"):
            inside, cur = True, {}
            continue
        if line.startswith("---------- End Simulation"):
            if inside:
                out.append(cur)
            inside = False
            continue
        if inside:
            m = re.match(r"^(\S+)\s+([-\d.]+(?:e[-+]?\d+)?)\s", line)
            if m:
                try:
                    cur[m.group(1)] = float(m.group(2))
                except ValueError:
                    pass
    return out


def pick(s, *keys):
    for k in keys:
        for n in s:
            if n == k or n.endswith("." + k):
                return s[n]
    return None


def row_key(r):
    size = r.get("bytesPerCall") or r.get("primeSizePerCall") or 0
    return r["description"] + (f" [{size} B]" if r.get("bytesPerCall")
                               else (f" [{size}-bit]" if r.get("primeSizePerCall") else ""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sweeps", nargs="+")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    rows, cells = {}, 0
    for base in a.sweeps:
        for d in sorted(os.listdir(base)):
            p = os.path.join(base, d)
            if not os.path.isdir(p) or not os.path.exists(os.path.join(p, "run.log")):
                continue
            try:
                arm, cfg, _grp = d.split("__")
            except ValueError:
                continue
            key = MAP.get((arm, cfg))
            if key is None:
                continue
            ds = dumps(os.path.join(p, "stats.txt"))
            log = open(os.path.join(p, "run.log"), errors="replace").read()
            cells += 1
            for m in ROW.findall(log):
                try:
                    r = json.loads(m)
                except ValueError:
                    continue
                i, n = int(r.get("roi", -1)), r.get("numCalls") or 0
                if not (0 <= i < len(ds)) or not n:
                    continue
                s = ds[i]
                cyc = pick(s, "core.numCycles", "numCycles")
                if not cyc:
                    continue
                ins = pick(s, "commitStats0.numInsts", "simInsts") or 0
                k = row_key(r)
                e = rows.setdefault(k, {"key": k, "family": r["description"],
                                        "size": r.get("bytesPerCall") or r.get("primeSizePerCall"),
                                        "cycles": {}, "instrs": {}, "ipc": {}, "ditw": {},
                                        "suspect": [], "n": 0})
                e["cycles"][key] = cyc / n
                e["instrs"][key] = ins / n
                e["ipc"][key] = (ins / cyc) if cyc else None
                e["ditw"][key] = (pick(s, "commit.ditWrites") or 0.0) / n
                e["n"] += 1

    # arm A is the unhardened reference; gem5 proves it identical across models, so take either
    for base in a.sweeps:
        for d in sorted(os.listdir(base)):
            p = os.path.join(base, d)
            if not os.path.isdir(p) or not d.startswith("A__apple__"):
                continue
            if not os.path.exists(os.path.join(p, "run.log")):
                continue
            ds = dumps(os.path.join(p, "stats.txt"))
            log = open(os.path.join(p, "run.log"), errors="replace").read()
            for m in ROW.findall(log):
                try:
                    r = json.loads(m)
                except ValueError:
                    continue
                i, n = int(r.get("roi", -1)), r.get("numCalls") or 0
                if not (0 <= i < len(ds)) or not n:
                    continue
                cyc = pick(ds[i], "core.numCycles", "numCycles")
                k = row_key(r)
                if cyc and k in rows:
                    rows[k]["cycles"]["A"] = cyc / n
                    rows[k]["instrs"]["A"] = (pick(ds[i], "commitStats0.numInsts", "simInsts") or 0) / n

    for e in rows.values():
        base = e["cycles"].get("A")
        e["pct"] = {k: (v / base - 1) * 100 for k, v in e["cycles"].items() if k != "A"} if base else {}

    full = {k: v for k, v in rows.items() if {"C", "B", "H", "Be", "He"} <= set(v["cycles"])}
    out = {"rows": full,
           "validity": {"runs_count": 1, "reps": 1, "timeout_ms": None, "platform": "gem5",
                        "runs": [{"source": os.path.abspath(s), "failures": 0,
                                  "rows": len(full)} for s in a.sweeps]},
           "provenance": {"cells": cells, "sweeps": [os.path.abspath(s) for s in a.sweeps]},
           "geomeans": {}, "anomalies": [], "series": {}, "prices": {}}
    json.dump(out, open(a.out, "w"), indent=1)
    print(f"{cells} cells -> {len(rows)} rows seen, {len(full)} complete in all five arms")
    miss = {k for k, v in rows.items() if k not in full}
    if miss:
        print(f"  incomplete ({len(miss)}), e.g.: " + ", ".join(sorted(miss)[:4]))
    print(f"wrote {a.out}")


if __name__ == "__main__":
    main()
