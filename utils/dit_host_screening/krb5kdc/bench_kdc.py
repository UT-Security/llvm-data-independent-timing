#!/usr/bin/env python3
"""Experiment 13: the MIT krb5 KDC under four arms on Apple silicon.

Per rep: start krb5kdc (foreground) under one arm, wait until it accepts TCP, run a fixed
load against it, SIGTERM it, and read its CPU time from wait4 rusage. The arms differ only
in the environment: which libk5crypto the dynamic linker resolves (DYLD_LIBRARY_PATH) and
whether the injected constructor sets DIT before main (ENABLE_DIT). The constructor also
reads PSTATE.DIT back at exit: C must exit with it set, every other arm clear.

  A   base libk5crypto                       unhardened
  C   base, ENABLE_DIT=1                     blanket
  B   libk5crypto with the developer's bracket on its 29 computing entry points
  Bn  the same with every mrs/msr/sb a hint #0 (layout control for B)

  bench_kdc.py [row ...]     rows: idle (no requests), tgs (TGS-REQ only), mix (kdc5_hammer: AS-REQ + TGS-REQ 1:1)
Env: W, REPS (default 15), WARM (default 2), TGS_N (default 4000), HAMMER_N/HAMMER_R (20 / 10)
"""
import os, re, sys, time, json, socket, signal, subprocess, statistics as st

W = os.path.expanduser(os.environ.get('W', '~/Documents/dit-krb5'))
P, RD, PORT = f'{W}/krb5-base', f'{W}/realm', 8888
REPS, WARM = int(os.environ.get('REPS', 15)), int(os.environ.get('WARM', 2))
TGS_N = int(os.environ.get('TGS_N', 4000))
HAMMER_N, HAMMER_R = int(os.environ.get('HAMMER_N', 20)), int(os.environ.get('HAMMER_R', 10))
ARMS = [('A', f'{P}/lib', 0), ('C', f'{P}/lib', 1), ('B', f'{W}/lib-bracket', 0), ('Bn', f'{W}/lib-bracketnop', 0)]
if os.environ.get('BENCH_ARMS'):
    keep = os.environ['BENCH_ARMS'].split(','); ARMS = [a for a in ARMS if a[0] in keep]
OUT = f'{W}/results'; os.makedirs(OUT, exist_ok=True)
BASEENV = dict(PATH=os.environ.get('PATH', ''), HOME=os.environ.get('HOME', ''),
               KRB5_CONFIG=f'{RD}/krb5.conf', KRB5_KDC_PROFILE=f'{RD}/kdc.conf')

def start_kdc(arm):
    env = dict(BASEENV, DYLD_LIBRARY_PATH=arm[1], DYLD_INSERT_LIBRARIES=f'{W}/libditctl.dylib', ENABLE_DIT=str(arm[2]))
    p = subprocess.Popen([f'{P}/sbin/krb5kdc', '-n', '-r', 'DIT.TEST'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    for _ in range(200):
        try:
            with socket.create_connection(('127.0.0.1', PORT), timeout=0.05): return p
        except OSError: time.sleep(0.02)
        if p.poll() is not None: break
    raise SystemExit(f'KDC did not come up under {arm[0]}: {p.stderr.read()[-300:]!r}')

def stop_kdc(p):
    p.send_signal(signal.SIGTERM)
    _, status, ru = os.wait4(p.pid, 0)
    err = p.stderr.read()
    m = re.search(rb'dit=([01])', err)
    return (ru.ru_utime + ru.ru_stime) * 1e3, (m.group(1).decode() if m else '?'), os.WEXITSTATUS(status) if os.WIFEXITED(status) else -1

def load_tgs():
    t0 = time.perf_counter_ns()
    r = subprocess.run([f'{W}/tgs_loop', 'user', 'userpw', 'host/svc.dit.test', str(TGS_N)], env=BASEENV, capture_output=True)
    return (time.perf_counter_ns() - t0) / 1e6, r.returncode == 0 and b'ok' in r.stdout, r.stderr[-200:]

def load_mix():
    t0 = time.perf_counter_ns()
    r = subprocess.run([f'{W}/build-base/tests/hammer/kdc5_hammer', '-p', 'hammer', '-n', str(HAMMER_N), '-R', str(HAMMER_R),
                        '-r', 'DIT.TEST', '-c', f'FILE:{RD}/hammer.cc'], env=BASEENV, capture_output=True)
    return (time.perf_counter_ns() - t0) / 1e6, r.returncode == 0, r.stderr[-200:]

ROWS = {'idle': ('start-up and shut-down only, no requests (the fixed cost inside every row)', lambda: (0.0, True, b'')),
        'tgs': (f'TGS-REQ only, {TGS_N} service-ticket requests', load_tgs),
        'mix': (f'kdc5_hammer, {HAMMER_N} principals x {HAMMER_R} rounds: AS-REQ + TGS-REQ 1:1', load_mix)}
want = sys.argv[1:] or list(ROWS)
results = {}
for key in want:
    name, load = ROWS[key]
    data = {a[0]: [] for a in ARMS}; wall = {a[0]: [] for a in ARMS}; gate = set(); bad = []
    for i in range(WARM + REPS):
        for arm in ARMS[i % len(ARMS):] + ARMS[:i % len(ARMS)]:
            kdc = start_kdc(arm)
            w, ok, err = load()
            cpu, dit, rc = stop_kdc(kdc)
            if not ok or rc != 0:
                bad.append((arm[0], rc, err)); continue
            gate.add((arm[0], arm[2], dit))
            if i >= WARM:
                data[arm[0]].append(cpu); wall[arm[0]].append(w)
    med = {a: st.median(v) for a, v in data.items() if v}
    if 'A' not in med:
        print(f'\n{name}: NO DATA, failures {bad[:3]}'); continue
    if 'C' not in med: med['C'] = med['A']
    mad = st.median([abs(x - med['A']) for x in data['A']]) / med['A'] * 100
    gate_ok = all(seen == str(d) for _, d, seen in gate)
    print(f"\n== kdc: {name}   ({len(data['A'])} reps/arm, MAD(A) {mad:.2f}%, gate {'ok' if gate_ok else sorted(gate)}, failures {len(bad)})")
    print(f"{'arm':<5}{'kdc cpu ms':>12}{'vs A':>9}{'vs C':>9}{'client wall ms':>16}")
    for a in med:
        print(f"{a:<5}{med[a]:>12.1f}{(med[a]/med['A']-1)*100:>+8.2f}%{(med[a]/med['C']-1)*100:>+8.2f}%{st.median(wall[a]):>16.0f}")
    if 'Bn' in med and 'B' in med:
        print(f"  executed-switch term: B-Bn {(med['B']-med['Bn'])/med['A']*100:+.2f} pts of A")
    if bad: print(f"  failures: {bad[:3]}")
    results[key] = dict(name=name, median=med, mad=mad, gate=sorted(gate), failures=len(bad),
                        samples=data, wall={a: st.median(v) for a, v in wall.items() if v})
    sys.stdout.flush()
json.dump(results, open(f'{OUT}/kdc_{"_".join(want)}.json', 'w'), indent=1)
