#!/usr/bin/env python3
"""Always-on DIT screen for Bitcoin Core, per benchmark.

Same rig as the five-host screen: ONE binary, DIT injected by
DYLD_INSERT_LIBRARIES at process load, so no codegen differs between arms and
dit-measurement-traps trap 7b cannot apply.

  base  no dylib
  null  dit_off.dylib  -> harness cost
  dit   dit_on.dylib   -> blanket DIT

bench_bitcoin reports ns/op per benchmark, so this gives per-workload DIT
sensitivity directly rather than one number for the whole binary. That matters
here because the benchmarks split cleanly into:
  PUBLIC  CCoinsCaching, DeserializeBlockTest, CheckBlockTest, ComplexMemPool,
          MemPoolAddTransactions, TxGraphTrim, CoinSelection, WalletAvailableCoins
  MIXED   ConnectBlockAllEcdsa, WalletCreateTxUsePresetInputsAndCoinSelection
  SECRET  SignTransactionECDSA, SignTransactionSchnorr
Condition (d) is decided by the PUBLIC rows.

lvp_chase runs in-band every rep (trap 5): if DIT stops taking effect mid-sweep
a null result would otherwise be indistinguishable from a broken rig.
"""
import csv, os, re, subprocess, sys

SW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BTC = os.path.expanduser("~/Documents/bitcoin/build/bin/bench_bitcoin")
DIT_ON = os.path.join(SW, "sweep", "bin", "dit_on.dylib")
DIT_OFF = os.path.join(SW, "sweep", "bin", "dit_off.dylib")
PROBE = os.path.join(SW, "sweep", "bin", "dit_probe")

BENCHES = ("CCoinsCaching|DeserializeBlockTest|CheckBlockTest|ComplexMemPool|"
           "MemPoolAddTransactions|TxGraphTrim|CoinSelection|WalletAvailableCoins|"
           "ConnectBlockAllEcdsa|WalletCreateTxUsePresetInputsAndCoinSelection|"
           "SignTransactionECDSA|SignTransactionSchnorr")
ARMS = [("base", None), ("null", DIT_OFF), ("dit", DIT_ON)]
MINTIME = os.environ.get("BTC_MINTIME", "500")

reps = int(sys.argv[1]) if len(sys.argv) > 1 else 8
burnin = int(sys.argv[2]) if len(sys.argv) > 2 else 1
out = os.path.join(SW, "btc", "btc_screen.csv")
row_re = re.compile(r"\|\s*([\d,]+\.\d+)\s*\|\s*[\d,.]+\s*\|\s*([\d.]+)%\s*\|\s*[\d.]+\s*\|\s*`([^`]+)`")
prx = re.compile(r"const_ns_per_hop=([0-9.]+) perm_ns_per_hop=([0-9.]+)")

def run(argv, dylib):
    env = dict(os.environ); env.pop("DYLD_INSERT_LIBRARIES", None)
    if dylib: env["DYLD_INSERT_LIBRARIES"] = dylib
    return subprocess.run(argv, env=env, capture_output=True, text=True)

with open(out, "w", newline="") as fh:
    w = csv.writer(fh); w.writerow(["rep","bench","arm","ns_per_op","err_pct","probe_off","probe_on"])
    for r in range(reps + burnin):
        keep = r >= burnin
        poff = prx.search(run([PROBE, "20000000"], DIT_OFF).stdout)
        pon = prx.search(run([PROBE, "20000000"], DIT_ON).stdout)
        c_off = poff.group(1) if poff else ""; c_on = pon.group(1) if pon else ""
        order = ARMS[r % len(ARMS):] + ARMS[:r % len(ARMS)]
        for arm, dylib in order:
            p = run([BTC, f"-filter={BENCHES}", f"-min-time={MINTIME}"], dylib)
            if p.returncode != 0:
                print(f"  !! {arm} rc={p.returncode} {p.stderr[:200]}", flush=True); continue
            for m in row_re.finditer(p.stdout):
                if keep:
                    w.writerow([r-burnin, m.group(3), arm, m.group(1).replace(",",""), m.group(2), c_off, c_on])
        fh.flush(); print(f"rep {r-burnin if keep else 'burnin'} done", flush=True)
print("WROTE", out)
