#!/usr/bin/env python3
"""Experiment 14: `bssl speed` under six arms, PMC cycles and instructions per operation.

Each sample is one `bssl speed -json` process for one filter; the patched tool reports
PMC0 cycles and PMC1 retired instructions over its own timed loop, so every row yields
cycles/op, instructions/op and IPC without kperf. Arms rotate on every rep. The injected
constructor pins QoS and reads PSTATE.DIT back at exit (C must exit 1, the rest 0).
NO SAMPLE IS EVER DROPPED: anything suspicious is flagged, counted and saved with its value.

Single core, hard-pinned: `bssl speed` runs its timed loop on one thread, and the injected
utils/cio_ditctl.c binds that thread to logical CPU DITCTL_PIN_CPU (= PIN_CPU here) through
kern.sched_thread_bind_cpu, a development sysctl this kernel exposes (boot-arg enable_skstb=1)
and a root-only write, so the driver runs under sudo (reproduce.sh does that for this stage
alone). The library's exit line reports pinned=<cpu> (or -1); a process whose line does not
name PIN_CPU is counted and the run is reported as not pinned. The PMC-implied clock (cycles/us)
is recorded per sample and flagged outside the band, never used to exclude.

  A    rel                            unhardened
  C    rel, ENABLE_DIT=1              blanket: DIT set before main by the constructor, library never touches it
  B    dit                            AWS-LC's bracket as shipped: mrs; msr dit,#1 ... msr dit,#0 per entry point
  Bs   ditsb                          B with `sb` after the enable (Apple's recipe)
  H    dit, `-dit`                    AWS's mitigation: DIT set once for the run; each entry still mrs + msr dit,#1
  Hs   ditsb, `-dit`                  the same on the sb build: each entry still mrs + msr dit,#1 + sb

  bench_awslc.py [test-filter ...]
Env: W, PIN_CPU (9: a P-core on the 4P+6E M4, where CPUs 0-5 are the E cluster; set it EMPTY for a
     deliberately unpinned dry run with no bind gate), REPS (7), WARM (1),
     TIMEOUT_MS (50), CHUNKS (16,256,1350,8192,16384), BENCH_ARMS, BENCH_TESTS
"""
import os, re, sys, json, subprocess, statistics as st

W = os.path.expanduser(os.environ.get('W', '~/Documents/dit-awslc'))
REPS, WARM = int(os.environ.get('REPS', 7)), int(os.environ.get('WARM', 1))
PIN_CPU = int(os.environ['PIN_CPU']) if os.environ.get('PIN_CPU') else None   # PIN_CPU= (empty): deliberately unpinned, no gate
# NOTHING IS DROPPED. Every sample goes into the medians. The implied clock (PMC cycles / wall
# microseconds) is recorded for every sample and used only to FLAG: unpinned, a value outside the
# P-core band 4000-4700 MHz means an E-core or a migration; pinned, only a value no single P-core
# can produce (below 3000, the E-core ceiling, or above 4700) can mean anything, and a DVFS dip is
# not a bad sample because cycles per op does not depend on the clock. The count of flagged samples
# and their values are printed and saved; the reader decides what they mean.
CLOCK_LO, CLOCK_HI = (3000, 4700) if PIN_CPU is not None else (4000, 4700)
TIMEOUT_MS = os.environ.get('TIMEOUT_MS', '50'); CHUNKS = os.environ.get('CHUNKS', '16,256,1350,8192,16384')
ARMS = [('A', 'rel', 0, []), ('C', 'rel', 1, []), ('B', 'dit', 0, []), ('Bs', 'ditsb', 0, []),
        ('H', 'dit', 0, ['-dit']), ('Hs', 'ditsb', 0, ['-dit'])]
if os.environ.get('BENCH_ARMS'):          # an empty value (sudo -E env passes one) means "all"
    keep = os.environ['BENCH_ARMS'].split(','); ARMS = [a for a in ARMS if a[0] in keep]
DEFAULT_TESTS = 'AEAD-AES-128-GCM,AEAD-ChaCha20-Poly1305,AES-128,SHA-256,HMAC-SHA256,ECDSA P-256,X25519,Ed25519,ML-KEM-768,RNG'
TESTS = [t for t in (sys.argv[1:] or (os.environ.get('BENCH_TESTS') or DEFAULT_TESTS).split(',')) if t]
OUT = f'{W}/results'; os.makedirs(OUT, exist_ok=True)

def run(arm, test):
    env = {k: os.environ[k] for k in ('PATH', 'HOME') if k in os.environ}
    env.update(DYLD_INSERT_LIBRARIES=f'{W}/libditctl.dylib', ENABLE_DIT=str(arm[2]))
    if PIN_CPU is not None: env['DITCTL_PIN_CPU'] = str(PIN_CPU)     # utils/cio_ditctl.c: bind via kern.sched_thread_bind_cpu (root)
    else: env['DITCTL_PIN'] = '0'                                      # deliberately unpinned dry run
    cmd = [f'{W}/build-{arm[1]}/tool/bssl', 'speed', '-json', '-timeout_ms', TIMEOUT_MS, '-chunks', CHUNKS, '-filter', test] + arm[3]
    p = subprocess.run(cmd, env=env, capture_output=True, text=True)
    m = re.search(r'dit=([01])', p.stderr); dit = m.group(1) if m else '?'
    mp = re.search(r'pinned=(-?\d+)', p.stderr)                          # the library reports the CPU it bound, -1 if it could not
    pinned = PIN_CPU is None or (bool(mp) and int(mp.group(1)) == PIN_CPU)
    out = p.stdout; i, j = out.find('['), out.rfind(']')
    rows = json.loads(out[i:j + 1]) if i >= 0 and j > i else []
    return rows, dit, p.returncode, pinned, (mp.group(0) if mp else p.stderr[-120:])

def key(row):
    size = row.get('bytesPerCall') or row.get('primeSizePerCall') or 0
    return f"{row['description']}" + (f" [{size} B]" if row.get('bytesPerCall') else (f" [{size}-bit]" if row.get('primeSizePerCall') else ''))

samples = {}   # key -> arm -> list of (cycles/op, instrs/op, ns/op)
gate, bad, flagged, unpinned, nopmc, clocks, flagged_clocks = set(), [], 0, [], 0, [], []
for i in range(WARM + REPS):
    for arm in ARMS[i % len(ARMS):] + ARMS[:i % len(ARMS)]:
        for test in TESTS:
            rows, dit, rc, pinned, pinline = run(arm, test)
            if rc != 0 or not rows:
                bad.append((arm[0], test, rc)); continue
            if not pinned:
                unpinned.append((arm[0], test, pinline))               # kept; the run is reported as not pinned if this is non-empty
            gate.add((arm[0], arm[2], dit))
            if i < WARM: continue
            for r in rows:
                n, us, cyc, ins = r['numCalls'], r['microseconds'], r.get('cycles', 0), r.get('instructions', 0)
                if not n or not us: continue
                if not cyc: nopmc += 1; continue                       # no PMC value to use; the row's wall time is still in the tool's JSON
                mhz = cyc / us
                if not CLOCK_LO <= mhz <= CLOCK_HI: flagged += 1; flagged_clocks.append((round(mhz), arm[0], key(r)))
                clocks.append(mhz)
                samples.setdefault(key(r), {}).setdefault(arm[0], []).append((cyc / n, ins / n, us * 1000 / n))
    sys.stderr.write(f'rep {i + 1}/{WARM + REPS} done\n')

gate_ok = all(seen == str(d) for _, d, seen in gate)
arms = [a[0] for a in ARMS]
print(f"awslc speed: {len(ARMS)} arms, {WARM} warm-up + {REPS} reps, timeout {TIMEOUT_MS} ms per row, chunks {CHUNKS}")
if PIN_CPU is None: print("UNPINNED RUN (PIN_CPU empty): QoS only, no bind check; a dry run, not a result")
else: print(f"pinned to CPU {PIN_CPU} via kern.sched_thread_bind_cpu; processes NOT reporting that bind (kept; non-zero means this is not a pinned run): {len(unpinned)}" + (f" e.g. {unpinned[0]}" if unpinned else ""))
print(f"gate {'ok' if gate_ok else sorted(gate)}, failures {len(bad)}" + (f" e.g. {bad[0]}" if bad else "") +
      f", rows without PMC cycles: {nopmc}, samples FLAGGED (all kept) for implied clock outside {CLOCK_LO/1000:.1f}-{CLOCK_HI/1000:.1f} GHz: {flagged}")
if clocks:
    cs = sorted(clocks); print(f"implied clock of kept samples: min {cs[0]:.0f}  p10 {cs[len(cs)//10]:.0f}  median {st.median(cs):.0f}  max {cs[-1]:.0f} MHz ({len(cs)} samples)")
if flagged_clocks:
    ds = sorted(m for m, _, _ in flagged_clocks); print(f"flagged samples' implied clock: min {ds[0]}  median {st.median(ds):.0f}  max {ds[-1]} MHz; e.g. {flagged_clocks[:3]}")
if not samples: print("NO SAMPLES: every row failed or lacked PMC cycles; nothing below is a result")
hdr = f"{'row':44s}{'A cyc/op':>10s}{'IPC':>6s}" + ''.join(f"{a:>8s}" for a in arms if a != 'A') + f"{'Bs-B':>8s}{'B-H':>8s}{'MAD':>7s}"
print(hdr)
results = {}
for k in sorted(samples):
    d = samples[k]
    if 'A' not in d: continue
    med = {a: st.median(v[0] for v in d[a]) for a in d}
    ipc = st.median(v[1] for v in d['A']) / med['A']
    mad = st.median(abs(v[0] - med['A']) for v in d['A']) / med['A'] * 100
    pct = {a: (med[a] / med['A'] - 1) * 100 for a in med if a != 'A'}
    bsb = (med['Bs'] - med['B']) / med['A'] * 100 if 'Bs' in med and 'B' in med else float('nan')      # the barrier's price per op
    bh = (med['B'] - med['H']) / med['A'] * 100 if 'B' in med and 'H' in med else float('nan')          # the per-call clear, and the dwell gaps it opens
    line = f"{k[:44]:44s}{med['A']:>10.0f}{ipc:>6.2f}" + ''.join(f"{pct.get(a, float('nan')):>+7.1f}%" for a in arms if a != 'A') + f"{bsb:>+8.2f}{bh:>+8.2f}{mad:>6.2f}%"
    print(line)
    ins = {a: st.median(v[1] for v in d[a]) for a in d}                     # instructions per op, every arm
    ipc_all = {a: ins[a] / med[a] for a in d}                                # IPC = instructions / cycles, every arm
    results[k] = dict(median_cycles_per_op=med, median_instrs_per_op=ins, ipc=ipc_all, ipc_A=ipc, mad_pct=mad, pct_vs_A=pct,
                      Bs_minus_B_pts=bsb, B_minus_H_pts=bh,
                      abs_bracket_cycles=(med['B'] - med['A']) if 'B' in med else None,
                      abs_bracket_instrs=(ins['B'] - ins['A']) if 'B' in ins else None,
                      ns_per_op_A=st.median(v[2] for v in d['A']), n=len(d['A']))
json.dump(dict(results=results, gate=sorted(gate), failures=bad, flagged=flagged, flagged_clocks=flagged_clocks, all_clocks_mhz=[round(c) for c in clocks],
               clock_band=[CLOCK_LO, CLOCK_HI], no_pmc_rows=nopmc, unpinned=len(unpinned), pin_cpu=PIN_CPU, arms=arms, tests=TESTS,
               reps=REPS, timeout_ms=TIMEOUT_MS, chunks=CHUNKS), open(f'{OUT}/speed.json', 'w'), indent=1)
print(f"\nBs-B is the barrier's price per op, B-H the per-call clear (and the DIT-off gaps it opens), in points of A; "
      f"abs cycles per op in {OUT}/speed.json")
# second table: both counters for every arm. cycles = instructions / IPC, so the bracket's cost splits into
# the instructions it adds (a few per entry) and the IPC it destroys (the serialising writes drain the pipeline)
print(f"\n{'row':44s}{'instr/op A':>11s}" + ''.join(f"{'IPC ' + a:>9s}" for a in arms) + f"{'B-A instr':>10s}")
for k in sorted(results):
    r = results[k]; ipcs = r['ipc']
    print(f"{k[:44]:44s}{r['median_instrs_per_op']['A']:>11.0f}" + ''.join(f"{ipcs.get(a, float('nan')):>9.2f}" for a in arms) +
          f"{(r['abs_bracket_instrs'] if r['abs_bracket_instrs'] is not None else float('nan')):>+10.0f}")
