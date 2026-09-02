#!/usr/bin/env python3
"""Report for utils/taint_libsodium_sudo_run.sh.

Prints both rigs plus the validity gates, and refuses to present a row whose
gates failed rather than letting a plausible-looking number through.
"""
import collections, csv, os, statistics as st, sys

OUT = os.environ.get("OUT") or (sys.argv[1] if len(sys.argv) > 1 else ".")

_opt = "-O?"   # the driver optimisation level; read from provenance just below
# CIO's drivers are built at whatever CIO_OPT the run used (-O0 is their own
# choice, -O2 is how an application would build them); the level changes the
# numbers, so read it back rather than hardcoding a label in the heading.
try:
    for _l in open(os.path.join(OUT, "provenance.txt")):
        if _l.startswith("cio driver opt:"):
            _opt = _l.split(":", 1)[1].strip() or _opt
except OSError:
    pass
ORDER = ["A", "C", "P", "F", "X", "N", "Z"]
NAME = {"A": "unhardened", "C": "blanket DIT", "P": "pass (shipped)",
        "F": "whole-function", "X": "pass (old defaults)", "N": "pass (resolved)",
        "Z": "pass, switches NOPed (control for P)"}


def load(path, keyfn, valfn):
    d, dit = collections.defaultdict(list), collections.defaultdict(set)
    try:
        rows = list(csv.DictReader(open(path)))
    except FileNotFoundError:
        return d, dit
    for r in rows:
        d[keyfn(r)].append(valfn(r))
        dit[keyfn(r)].add(r.get("dit_exit", "?"))
    return d, dit


def table(title, d, dit, unit, blanket_exits_set):
    """blanket_exits_set: does arm C legitimately still have DIT set at exit?

    Part 1 drivers call their own dit_enable()/dit_disable() around the measured
    region, so even the blanket arm exits with DIT=0 -- correct, and the in-region
    DitBit metric is what proves the mode was on. Part 2 uses CIO's drivers, which
    have no DIT support, so blanket comes from the shim constructor and is never
    cleared: there arm C MUST exit with DIT=1. Applying one rule to both flagged
    every Part 1 row as a leak."""
    if not d:
        print(f"\n{title}: no data\n")
        return
    keys, arms = [], []
    for k, a in d:
        if k not in keys:
            keys.append(k)
        if a not in arms:
            arms.append(a)
    arms = [a for a in ORDER if a in arms]
    print(f"\n{'='*96}\n{title}\n{'='*96}")
    hdr = f"{'benchmark / metric':<34}" + "".join(f"{a:>11}" for a in arms)
    print(hdr + "     gate")
    print("-" * len(hdr) + "---------")
    for k in keys:
        med = {a: st.median(d[(k, a)]) for a in arms if (k, a) in d}
        if "A" not in med:
            continue
        base, bl = med["A"], med.get("C")
        # gate 4: DIT at exit must be 1 for the blanket arm and 0 for every other
        bad = []
        for a in arms:
            if (k, a) not in dit:
                continue
            seen_set = "1" in dit[(k, a)]
            want_set = (a == "C" and blanket_exits_set)
            if seen_set != want_set:
                bad.append(a)
        # Dispersion as MEDIAN ABSOLUTE DEVIATION, not (max-min)/min.
        # (max-min)/min is decided by a single outlier: on ed25519/Sign it read
        # 27.7% while the MAD was 0.60% and every arm's samples sat inside a 1%
        # band -- one hiccup per arm out of 25 reps. That crude measure flagged a
        # 14% effect with a 24:1 signal-to-noise ratio as "not resolvable".
        # MAD ignores the tail, which is what a median-based statistic should do.
        def _disp(v):
            m = st.median(v)
            return (st.median([abs(x - m) for x in v]) / m) if m else 0.0
        spread = max((_disp(d[(k, a)]) for a in med), default=0.0)
        lo = min(med.values())
        betw = (max(med.values()) - lo) / lo if lo > 0 else 0
        # resolvable when the between-arm range clears 3x the worst within-arm MAD
        g = "DIT-LEAK:" + ",".join(bad) if bad else (
            "unresolvable" if betw < 3 * spread else "ok")
        print(f"{str(k):<34}" + "".join(f"{med[a]:>11.0f}" if a in med else f"{'-':>11}"
                                        for a in arms) + f"     {g}")
        row = f"{'  vs unhardened':<34}"
        for a in arms:
            row += f"{(med[a]/base-1)*100:>+10.2f}%" if a in med and base else f"{'-':>11}"
        print(row)
        if bl:
            row = f"{'  vs blanket':<34}"
            for a in arms:
                row += f"{(med[a]/bl-1)*100:>+10.2f}%" if a in med else f"{'-':>11}"
            print(row)
        print(f"{'  MAD within / range between':<34}{spread*100:>10.1f}% {betw*100:>9.1f}%")
    print(f"\nunits: {unit}")


ours, ours_dit = load(os.path.join(OUT, "ours.csv"),
                      lambda r: (f"{r['benchmark']}/{r['metric']}", r['arm']),
                      lambda r: float(r['value']))
import csv as _csv
try:
    _rr = list(_csv.DictReader(open(os.path.join(OUT, "cio.csv"))))
    _src = {r.get("cycle_src", "?") for r in _rr}
    # timer_src is absent from runs made before the cheap-timer split; those
    # differenced the kperf read, so that is the correct fallback.
    _tsrc = {r.get("timer_src") or "kperf" for r in _rr}
except FileNotFoundError:
    _src = set(); _tsrc = set()
cio, cio_dit = load(os.path.join(OUT, "cio.csv"),
                    lambda r: (r['benchmark'], r['arm']),
                    lambda r: float(r['mean_ticks']))

print("\nARMS: " + " | ".join(f"{a}={NAME[a]}" for a in ORDER))

# gates 1-3 come from the ditprobe rows in part 1
probe = {k[0].split('/')[1]: {a: st.median(v) for (kk, a), v in ours.items()
                              if kk == k[0]} for k in ours if k[0].startswith('ditprobe/')}
if probe:
    print(f"\n{'='*96}\nINSTRUMENT GATES (ditprobe, interleaved with every other benchmark)\n{'='*96}")
    c, p = probe.get('Const', {}), probe.get('Perm', {})
    mhz, bit = probe.get('CoreMHz', {}), probe.get('DitBit', {})
    if c.get('A') and c.get('C'):
        r = c['C'] / c['A']
        # The RATIO is not portable and must not be the gate. Const-off is
        # value-predicted at ~1 cycle/hop on every host that predicts at all;
        # Const-on falls back to L1 load-to-use. So the ratio's CEILING is the
        # L1 latency in cycles -- 4 on M5, 3 on M4 -- and a fixed ">3.5x" test
        # is unsatisfiable on a 3-cycle-L1 core no matter how well DIT works.
        # Measured: M5 222->873 ps at 4597 MHz = 1.02->4.01 cyc/hop, 3.932x;
        # M4 227->680 ps at 4406 MHz = 1.00->3.00 cyc/hop, 2.996x. Same
        # phenomenon, and in both cases Const-on lands on top of Perm.
        # Gate on cycles/hop instead: predicted ~1, unpredicted ~= Perm.
        g = mhz.get('A') or mhz.get('C')
        if g:
            off, on = c['A'] * 1e-3 * g / 1e3, c['C'] * 1e-3 * g / 1e3
            pm = (p.get('A', 0) or 0) * 1e-3 * g / 1e3
            ok = off < 2.0 and (pm == 0 or on > pm * 0.9)
            print(f"  1. DIT visible          Const {off:.2f} -> {on:.2f} cyc/hop "
                  f"({r:.3f}x, Perm {pm:.2f}) "
                  f"{'PASS' if ok else 'FAIL - instrument cannot see DIT'}")
            print(f"       predicted <2 cyc/hop with DIT off, and DIT on must reach Perm")
        else:
            print(f"  1. DIT visible          Const A={c['A']:.0f} C={c['C']:.0f} -> {r:.3f}x "
                  f"(no CoreMHz row; cannot express in cyc/hop)")
    if p.get('A') and p.get('C'):
        r = p['C'] / p['A']
        print(f"  2. negative control     Perm  {r:.4f}x "
              f"{'PASS (flat)' if abs(r-1) < 0.02 else 'FAIL'}")
    if mhz:
        lo, hi = min(mhz.values()), max(mhz.values())
        print(f"  3. P-core residency     CoreMHz {lo:.0f}-{hi:.0f} "
              f"{'PASS' if lo > 4000 else 'FAIL - a rep ran below P-core clock (E-cluster, or DVFS ramp)'}")
    if bit:
        ok = bit.get('A') == 0 and bit.get('C') == 1
        print(f"  4. mode readback        DitBit A={bit.get('A')} C={bit.get('C')} "
              f"{'PASS' if ok else 'FAIL'}")

table("PART 1 - our 13 primitives (-O2 drivers)", ours, ours_dit,
      "cntvct_el0 ticks/op (~1 ns), or ps/hop for ditprobe. TIME, not cycles.",
      blanket_exits_set=False)
# What the driver DIFFERENCED is timer_src; cycle_src describes the counter
# accumulators only. They differ whenever CHEAP_TIMER=1, and it is the timer
# that sets the units of every number in this table.
if _tsrc == {"kperf"}:
    _u = ("REAL CYCLES via kperf -- each sample carries the ~3234-cycle region "
          "read offset, which cancels in arm differences but NOT in the "
          "percentages below (72-87% of the chacha/AES baselines). See "
          "utils/cio_offset_probe.c")
elif _tsrc == {"cntvct"}:
    _u = ("cntvct_el0 TICKS, not cycles -- see cntfrq_el0 in provenance.txt "
          "(1 ns/tick on M4; NOT hw.tbfrequency, which is the 24 MHz Mach "
          "timebase and disagrees). Region offset ~21 cycles, so the "
          "percentages are sound; the absolute column is TIME, not cycles")
else:
    _u = f"MIXED timer sources {_tsrc} -- do not compare across arms"
table(f"PART 2 - CIO's benchmarks, their parameters ({_opt} drivers, mean of "
      "per-iteration counts)", cio, cio_dit,
      _u + ", mean over their iteration count (CIO's own statistic)",
      blanket_exits_set=True)

# ---- counter table: what cntvct could never show -------------------------
try:
    _rows = list(_csv.DictReader(open(os.path.join(OUT, "cio.csv"))))
except FileNotFoundError:
    _rows = []
_c = collections.defaultdict(list)
for r in _rows:
    if r.get("tot_ins", "0") not in ("0", "", None):
        _c[(r["benchmark"], r["arm"])].append(
            (float(r["tot_cyc"]), float(r["tot_ins"]), float(r["map_stall"])))
if _c:
    print(f"\n{'='*96}\nCOUNTERS - whole-process totals per arm (kperf)\n{'='*96}")
    print(f"{'benchmark':<28}{'arm':<5}{'instructions':>15}{'cycles':>15}{'IPC':>8}"
          f"{'ins vs A':>10}{'cyc vs A':>10}")
    print("-" * 91)
    _b = []
    for (b, _a) in _c:
        if b not in _b:
            _b.append(b)
    for b in _b:
        base = None
        for a in ORDER:
            if (b, a) not in _c:
                continue
            cyc = st.median([x[0] for x in _c[(b, a)]])
            ins = st.median([x[1] for x in _c[(b, a)]])
            if a == "A":
                base = (cyc, ins)
            dc = f"{(cyc/base[0]-1)*100:+.2f}%" if base else "-"
            di = f"{(ins/base[1]-1)*100:+.2f}%" if base else "-"
            print(f"{b if a==ORDER[0] else '':<28}{a:<5}{ins:>15,.0f}{cyc:>15,.0f}"
                  f"{cyc/ins if ins else 0:>8.3f}{di:>10}{dc:>10}")
        print()
    print("""HOW TO READ THE COUNTERS
  A vs C instructions ~ 0%   the arms run the same work; blanket adds one MSR.
                             If this is not ~0 the cycle ratios are meaningless.
  A vs C cycles > 0          pure DWELL - identical instructions, more cycles,
                             i.e. the mode itself slowing execution down.
  A vs P instructions > 0    the switches selective placement actually executes.
  IPC = instructions/cycle   Higher is better. FALLING IPC with flat instruction
                             counts means the same work is stalling, not that more
                             work is being done -- a serialising switch draining the
                             pipeline. Instructions up at flat IPC would be switch
                             COUNT instead. cntvct cannot separate these; pair the
                             IPC change with map_stall to confirm the mechanism.""")

print("""
READING IT
  C/A ~ 1.00  ->  blanket DIT is FREE on this workload. Then there is no prize,
                  and every switch a selective policy adds is pure loss. That is
                  a property of the workload, not a refutation of the approach.
  P vs C      ->  the question the paper asks. P > C means blanket wins.
  X vs P      ->  what the 2026-08-24 default change bought.
  N vs P      ->  what indirect-call resolution costs when dwell is ~0.
Any row marked DIT-LEAK is NOT a result: an arm ran in a mode it does not claim.
""")
