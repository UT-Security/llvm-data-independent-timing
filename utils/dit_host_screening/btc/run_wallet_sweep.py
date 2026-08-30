#!/usr/bin/env python3
"""Bitcoin wallet secret-fraction sweep: does the crossover land where predicted?

ONE unmodified call -- CreateTransaction on WalletCreateTxUsePresetInputsAndCoin-
Selection -- containing both lanes in the order the wallet runs them: coin
selection (public, +13% under blanket DIT, all of it the EVES value predictor)
then CKey::Sign per input (secret). BTC_BENCH_INPUTS sets how many signatures
happen per selection and is the only thing that varies across points.

Design notes in docs/paper/bitcoin-secret-fraction-sweep.md.

EIGHT ARMS PER POINT. The usual six, plus two that run the same call with
BTC_BENCH_SIGN=0 -- CreateTransaction's own `sign` parameter, the path
fundrawtransaction and PSBT creation use. Those two are what make f_secret a
measurement rather than a model:

  baseline    nodit, no dylib,  sign=1   round-trip control (empty seed file)
  null        nodit, dit_off,   sign=1   harness cost, DIT never written
  always      nodit, dit_on,    sign=1   blanket DIT over the whole call
  pass        gated, no dylib,  sign=1   the shipped pass
  passnop     nop,   no dylib,  sign=1   same placement, switches are HINT #0
  baseline2   nodit, no dylib,  sign=1   noise floor
  pub_base    nodit, no dylib,  sign=0   the public lane alone
  pub_always  nodit, dit_on,    sign=0   blanket DIT on the public lane alone

Derived at every K:
  f_secret  = (baseline - pub_base) / baseline        measured, not assumed
  C_public  = pub_always / pub_base - 1               the prize, isolated
  C_whole   = always / baseline - 1
  crossover = pass / always - 1                       negative = pass wins
  switches  = pass / passnop - 1

And the gate that makes the whole thing falsifiable:

  C_whole ~= (1 - f)*C_public + f*C_secret

Solving for C_secret at each K must land near the independently measured
SignTransactionECDSA figure (+3.39%). If it does not, the arms are not measuring
what their names claim -- the framework's "region and whole-program arithmetic
must close" detector, applied across a sweep instead of a single point.

Arm order rotates every rep; dit_probe runs in-band every rep and its const
chase must read ~4x or DIT stopped taking effect and the run is void.

Usage: run_wallet_sweep.py [reps] [burnin]
"""
import csv, os, re, subprocess, sys

SW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.dirname(os.path.abspath(__file__))
BTC = os.path.expanduser("~/Documents/bitcoin")
BN = f"{BTC}/build-nodit-v2/bin/bench_bitcoin"
BG = f"{BTC}/build-gated-v2/bin/bench_bitcoin"
BP = f"{BTC}/build-nop-v2/bin/bench_bitcoin"
BIN = os.path.join(SW, "sweep", "bin")
ON, OFF = os.path.join(BIN, "dit_on.dylib"), os.path.join(BIN, "dit_off.dylib")
PROBE = os.path.join(BIN, "dit_probe")

BENCH = "WalletCreateTxUsePresetInputsAndCoinSelection"
MT = os.environ.get("BTC_MINTIME", "2000")
INPUTS = [int(x) for x in os.environ.get("BTC_SWEEP_INPUTS", "1,4,10,25,50,100,200,400").split(",")]

# (name, binary, dylib, sign)
ARMS = [
    ("baseline",   BN, None, 1),
    ("null",       BN, OFF,  1),
    ("always",     BN, ON,   1),
    ("pass",       BG, None, 1),
    ("passnop",    BP, None, 1),
    ("baseline2",  BN, None, 1),
    ("pub_base",   BN, None, 0),
    ("pub_always", BN, ON,   0),
]

row = re.compile(r"\|\s*([\d,]+\.\d+)\s*\|\s*[\d,.]+\s*\|\s*([\d.]+)%\s*\|\s*[\d.]+\s*\|\s*`([^`]+)`")
prx = re.compile(r"const_ns_per_hop=([0-9.]+) perm_ns_per_hop=([0-9.]+)")


def run(argv, dylib, env_extra):
    env = dict(os.environ)
    env.pop("DYLD_INSERT_LIBRARIES", None)
    env.update(env_extra)
    if dylib:
        env["DYLD_INSERT_LIBRARIES"] = dylib
    return subprocess.run(argv, env=env, capture_output=True, text=True)


def main():
    reps = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    burn = int(sys.argv[2]) if len(sys.argv) > 2 else 1

    arms = [a for a in ARMS if os.path.exists(a[1])]
    missing = {a[0] for a in ARMS} - {a[0] for a in arms}
    if missing:
        sys.exit(f"missing binaries for arms: {sorted(missing)}")

    out = os.path.join(HERE, os.environ.get("BTC_OUT", "wallet_sweep.csv"))
    with open(out, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["rep", "inputs", "arm", "ns_per_op", "err", "probe_off", "probe_on"])
        for r in range(reps + burn):
            keep = r >= burn
            po = prx.search(run([PROBE, "20000000"], OFF, {}).stdout)
            pn = prx.search(run([PROBE, "20000000"], ON, {}).stdout)
            co = po.group(1) if po else ""
            cn = pn.group(1) if pn else ""
            for K in INPUTS:
                order = arms[r % len(arms):] + arms[:r % len(arms)]
                for arm, binry, dy, sign in order:
                    env = {"BTC_BENCH_INPUTS": str(K), "BTC_BENCH_SIGN": str(sign)}
                    p = run([binry, f"-filter={BENCH}", f"-min-time={MT}"], dy, env)
                    if p.returncode != 0:
                        print(f"  !! K={K} {arm} rc={p.returncode} {p.stderr[:150]}", flush=True)
                        continue
                    for m in row.finditer(p.stdout):
                        if keep:
                            w.writerow([r - burn, K, arm,
                                        m.group(1).replace(",", ""), m.group(2), co, cn])
            fh.flush()
            print(f"rep {r - burn if keep else 'burnin'} done", flush=True)
    print("WROTE", out)


if __name__ == "__main__":
    main()
