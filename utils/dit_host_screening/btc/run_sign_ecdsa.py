#!/usr/bin/env python3
"""Bitcoin Core SignTransactionECDSA: always-on DIT vs the taint pass, on silicon.

ONE benchmark, six arms. SignTransactionECDSA is the toggle-bound end of the
Bitcoin set: per iteration CKey::Sign does ~2 ECDSA signs (low-R grinding), a
pubkey_create and an ecdsa_verify, so the secret fraction is ~100% and the pass
has almost no public work to protect -- it can only pay for the switches it
inserts. That is the case where blanket DIT is expected to win on silicon, and
the case gem5 is supposed to change, because gem5's `msr dit` is rename-resolved
rather than serializing.

  baseline   build-nodit-v2, no dylib   round-trip control: the pass ran with an
                                        EMPTY seed file, so the MIR made the same
                                        trip and placed nothing. Controls the
                                        codegen lottery (traps: round-trip).
  null       build-nodit-v2 + dit_off   harness cost: the dylib is loaded and
                                        interposes pthread_create, but never
                                        writes DIT.
  always     build-nodit-v2 + dit_on    blanket DIT, same binary as baseline.
  pass       build-gated-v2, no dylib   the shipped pass: 9 seeds, region
                                        placement, switch-cyc=30, loop hoist and
                                        the mod-set call-site gate, all defaults.
  passnop    build-nop-v2,   no dylib   -taint-dit-nop-switches: byte-identical
                                        placement and layout to `pass`, every
                                        MSR DIT emitted as HINT #0. pass-passnop
                                        is the switch's own cost; passnop-baseline
                                        is what the extra instructions and the
                                        changed layout cost. Without this arm a
                                        gem5/silicon gap cannot be attributed.
  baseline2  build-nodit-v2, no dylib   noise floor: a second run of an
                                        instruction-identical arm. Any effect
                                        smaller than |baseline2| is not real.

Arm order is rotated every rep so drift cannot masquerade as an arm effect, and
dit_probe runs in-band every rep: its const chase must read ~4x between the
dylibs or DIT stopped taking effect and every number here is void.

Usage: run_sign_ecdsa.py [reps] [burnin]
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

# Which benchmark to measure. SignTransactionECDSA by default; set BTC_BENCH
# to measure another from the same six-arm rig (e.g. CoinSelection).
BENCH = os.environ.get("BTC_BENCH", "SignTransactionECDSA")
MT = os.environ.get("BTC_MINTIME", "500")

ARMS = [("baseline", BN, None), ("null", BN, OFF), ("always", BN, ON),
        ("pass", BG, None), ("passnop", BP, None), ("baseline2", BN, None)]

row = re.compile(r"\|\s*([\d,]+\.\d+)\s*\|\s*[\d,.]+\s*\|\s*([\d.]+)%\s*\|\s*[\d.]+\s*\|\s*`([^`]+)`")
prx = re.compile(r"const_ns_per_hop=([0-9.]+) perm_ns_per_hop=([0-9.]+)")


def run(argv, dylib):
    env = dict(os.environ)
    env.pop("DYLD_INSERT_LIBRARIES", None)
    if dylib:
        env["DYLD_INSERT_LIBRARIES"] = dylib
    return subprocess.run(argv, env=env, capture_output=True, text=True)


def main():
    reps = int(sys.argv[1]) if len(sys.argv) > 1 else 15
    burn = int(sys.argv[2]) if len(sys.argv) > 2 else 1

    arms = [a for a in ARMS if os.path.exists(a[1])]
    missing = {a[0] for a in ARMS} - {a[0] for a in arms}
    if missing:
        print(f"!! skipping arms with no binary: {sorted(missing)}", flush=True)
    if not arms:
        sys.exit("no arms available")

    out = os.path.join(HERE, os.environ.get("BTC_OUT", "sign_ecdsa.csv"))
    with open(out, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["rep", "bench", "arm", "ns_per_op", "err", "probe_off", "probe_on"])
        for r in range(reps + burn):
            keep = r >= burn
            po = prx.search(run([PROBE, "20000000"], OFF).stdout)
            pn = prx.search(run([PROBE, "20000000"], ON).stdout)
            co = po.group(1) if po else ""
            cn = pn.group(1) if pn else ""
            order = arms[r % len(arms):] + arms[:r % len(arms)]
            for arm, binry, dy in order:
                p = run([binry, f"-filter={BENCH}", f"-min-time={MT}"], dy)
                if p.returncode != 0:
                    print(f"  !! {arm} rc={p.returncode} {p.stderr[:150]}", flush=True)
                    continue
                for m in row.finditer(p.stdout):
                    if keep:
                        w.writerow([r - burn, m.group(3), arm,
                                    m.group(1).replace(",", ""), m.group(2), co, cn])
            fh.flush()
            print(f"rep {r - burn if keep else 'burnin'} done", flush=True)
    print("WROTE", out)


if __name__ == "__main__":
    main()
