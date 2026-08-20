#!/usr/bin/env python3
"""End-to-end always-on DIT cost: Bitcoin Core -reindex-chainstate, real mainnet.

Chain: mainnet blocks to height 200,000 (7.3M txs, 3.7 GB), downloaded by IBD.
Under the default -assumevalid, validation.cpp:2504 sets fScriptChecks=false
below the assumed-valid block, so this run does ZERO ECDSA/Schnorr verification.
It is pure public work: LevelDB LSM, CCoinsMap hash probing, block
deserialization, undo-file I/O. Secret fraction is ~0, so this measures the
PRIZE (what always-on DIT costs on code the pass would leave unprotected), not
the win.

bitcoind is multi-threaded, so the injector's pthread_create interposition is
what makes the `dit` arm meaningful; the exit line reports threads_started vs
threads_total and any gap is honest under-coverage (libdispatch workers are not
created via pthread_create).
"""
import csv, os, re, subprocess, sys, time

SW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BTCD = os.path.expanduser("~/Documents/bitcoin/build/bin/bitcoind")
DATA = os.path.expanduser("~/btcdata")
DIT_ON = os.path.join(SW, "sweep", "bin", "dit_on.dylib")
DIT_OFF = os.path.join(SW, "sweep", "bin", "dit_off.dylib")
PROBE = os.path.join(SW, "sweep", "bin", "dit_probe")

ARMS = [("base", None), ("null", DIT_OFF), ("dit", DIT_ON)]
ARGS = [BTCD, f"-datadir={DATA}", "-reindex-chainstate",
        "-stopatheight=200000", "-dbcache=2000", "-printtoconsole=0"]
prx = re.compile(r"const_ns_per_hop=([0-9.]+) perm_ns_per_hop=([0-9.]+)")
thr = re.compile(r"threads_started=(\d+) threads_total=(\d+)")

reps = int(sys.argv[1]) if len(sys.argv) > 1 else 12
burnin = int(sys.argv[2]) if len(sys.argv) > 2 else 1
out = os.path.join(SW, "btc", "reindex.csv")

def run(argv, dylib):
    env = dict(os.environ); env.pop("DYLD_INSERT_LIBRARIES", None)
    if dylib: env["DYLD_INSERT_LIBRARIES"] = dylib
    t0 = time.perf_counter()
    p = subprocess.run(argv, env=env, capture_output=True, text=True)
    return time.perf_counter() - t0, p

with open(out, "w", newline="") as fh:
    w = csv.writer(fh); w.writerow(["rep","arm","seconds","rc","thr_started","thr_total","probe_off","probe_on"])
    for r in range(reps + burnin):
        keep = r >= burnin
        _, po = run([PROBE, "20000000"], DIT_OFF); _, pn = run([PROBE, "20000000"], DIT_ON)
        mo, mn = prx.search(po.stdout), prx.search(pn.stdout)
        c_off = mo.group(1) if mo else ""; c_on = mn.group(1) if mn else ""
        order = ARMS[r % len(ARMS):] + ARMS[:r % len(ARMS)]
        for arm, dylib in order:
            secs, p = run(ARGS, dylib)
            m = thr.search(p.stderr or "")
            if p.returncode != 0:
                print(f"  !! {arm} rc={p.returncode} {p.stderr[:150]}", flush=True); continue
            if keep:
                w.writerow([r-burnin, arm, f"{secs:.3f}", p.returncode,
                            m.group(1) if m else "", m.group(2) if m else "", c_off, c_on])
        fh.flush(); print(f"rep {r-burnin if keep else 'burnin'} done", flush=True)
print("WROTE", out)
