#!/usr/bin/env python3
"""Bitcoin Core: does the taint pass beat always-on DIT?

  baseline    build-nodit (round-trip control, 0 switches), no dylib
  null        build-nodit + dit_off.dylib        -> harness cost
  always      build-nodit + dit_on.dylib         -> blanket DIT
  pass_hoist  build-hoist (541 switches, 9 entry points seeded), no dylib
  pass_gated  build-gated (178 switches: same seeds + -taint-modset-callsite-gated)
  pass_fa     build-fa      (-taint-frame-addr-args: closes the &secret_local under-taint)
  pass_fagate build-fagated (-taint-frame-addr-args + -taint-modset-callsite-gated)

The fallback pair is the point of this run: the gate tests argument REGISTERS, so
with the fallback off it suppresses the clobber at exactly the call sites the
fallback exists to catch. Measuring them together says whether the composition is
the configuration to ship.
  baseline2   second run of baseline             -> noise floor

Both builds went through the same MIR round-trip, so the codegen lottery is
controlled for. Arm order rotated. lvp_chase in-band every rep.
"""
import csv, os, re, subprocess, sys
SW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BN = os.path.expanduser("~/Documents/bitcoin/build-nodit/bin/bench_bitcoin")
BH = os.path.expanduser("~/Documents/bitcoin/build-hoist/bin/bench_bitcoin")
BG = os.path.expanduser("~/Documents/bitcoin/build-gated/bin/bench_bitcoin")
BF = os.path.expanduser("~/Documents/bitcoin/build-fa/bin/bench_bitcoin")
BFG = os.path.expanduser("~/Documents/bitcoin/build-fagated/bin/bench_bitcoin")
ON = os.path.join(SW,"sweep","bin","dit_on.dylib"); OFF = os.path.join(SW,"sweep","bin","dit_off.dylib")
PROBE = os.path.join(SW,"sweep","bin","dit_probe")
BENCH=("WalletCreateTxUsePresetInputsAndCoinSelection|WalletCreateTxUseOnlyPresetInputs|"
       "CoinSelection|WalletAvailableCoins|SignTransactionECDSA|SignTransactionSchnorr|"
       "ConnectBlockAllEcdsa|ComplexMemPool|TxGraphTrim")
ARMS=[("baseline",BN,None),("null",BN,OFF),("always",BN,ON),("pass_hoist",BH,None),("pass_gated",BG,None),("pass_fa",BF,None),("pass_fagate",BFG,None),("baseline2",BN,None)]
MT=os.environ.get("BTC_MINTIME","500")
reps=int(sys.argv[1]) if len(sys.argv)>1 else 15
burn=int(sys.argv[2]) if len(sys.argv)>2 else 1
out=os.path.join(SW,"btc","btc_fa.csv")
row=re.compile(r"\|\s*([\d,]+\.\d+)\s*\|\s*[\d,.]+\s*\|\s*([\d.]+)%\s*\|\s*[\d.]+\s*\|\s*`([^`]+)`")
prx=re.compile(r"const_ns_per_hop=([0-9.]+) perm_ns_per_hop=([0-9.]+)")
def run(argv,dy):
    e=dict(os.environ); e.pop("DYLD_INSERT_LIBRARIES",None)
    if dy: e["DYLD_INSERT_LIBRARIES"]=dy
    return subprocess.run(argv,env=e,capture_output=True,text=True)
with open(out,"w",newline="") as fh:
    w=csv.writer(fh); w.writerow(["rep","bench","arm","ns_per_op","err","probe_off","probe_on"])
    for r in range(reps+burn):
        keep=r>=burn
        po=prx.search(run([PROBE,"20000000"],OFF).stdout); pn=prx.search(run([PROBE,"20000000"],ON).stdout)
        co=po.group(1) if po else ""; cn=pn.group(1) if pn else ""
        order=ARMS[r%len(ARMS):]+ARMS[:r%len(ARMS)]
        for arm,binry,dy in order:
            p=run([binry,f"-filter={BENCH}",f"-min-time={MT}"],dy)
            if p.returncode!=0: print(f"  !! {arm} rc={p.returncode} {p.stderr[:150]}",flush=True); continue
            for m in row.finditer(p.stdout):
                if keep: w.writerow([r-burn,m.group(3),arm,m.group(1).replace(",",""),m.group(2),co,cn])
        fh.flush(); print(f"rep {r-burn if keep else 'burnin'} done",flush=True)
print("WROTE",out)
