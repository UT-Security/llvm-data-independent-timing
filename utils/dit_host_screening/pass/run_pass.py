#!/usr/bin/env python3
"""Does the taint pass reach the hand-placed oracle?

Arms (all compared to `nodit`, the round-trip control, NEVER to a stock -O2
build - dit-measurement-traps trap 7):

  nodit@off      baseline: taint pipeline ran, empty seed, 0 MSR DIT emitted
  nodit@always   blanket protection, set at process start
  nodit@oracle   hand-placed oracle, 2 switches per signature
  region@off     THE PASS, shipped default placement (148 switches in the TU)
  hoist@off      THE PASS + -taint-dit-loop-hoist=1 (136)
  function@off   THE PASS + -taint-dit-placement=function (122)
  nodit@off2     a second baseline run - the rig's own noise floor

Only secp256k1.c is instrumented; the hosts are stock. The secret key reaches
the program solely through secp256k1_ecdsa_sign arg 3, which is the seed.

Arm order is rotated every rep: a fixed order silently penalises whichever arm
runs last (see dit-oracle-composites.md).
"""
import csv
import os
import re
import subprocess
import sys
import time

P = os.path.dirname(os.path.abspath(__file__))
SW = os.path.dirname(P)
SEC = os.path.join(SW, "sweep", "secret")
CP = os.path.join(SW, "sweep", "src", "cpython")

ENV_CPY = {"PYTHONHOME": CP,
           "PYTHONPATH": os.path.join(CP, "Lib") + ":" +
                         os.path.join(CP, "build", "lib.macosx-26.6-arm64-3.16")}

# (arm label, binary variant, runtime DIT mode)
ARMS = [
    ("baseline",  "nodit",    "0"),
    ("always",    "nodit",    "1"),
    ("oracle",    "nodit",    "2"),
    ("pass_region",   "region",   "0"),
    ("pass_hoist",    "hoist",    "0"),
    ("pass_function", "function", "0"),
    ("baseline2", "nodit",    "0"),
]

HOSTS = [
    ("cpython", lambda v, m: ([os.path.join(P, f"host_cpython_{v}"), "work.py", m, "1"], ENV_CPY)),
    ("sqlite",  lambda v, m: ([os.path.join(P, f"host_sqlite_{v}"), m, "120000", "14", "1"], {})),
]

reps = int(sys.argv[1]) if len(sys.argv) > 1 else 16
burnin = int(sys.argv[2]) if len(sys.argv) > 2 else 2
out = os.path.join(P, "pass_results.csv")

rx = re.compile(r"total_s=([0-9.]+) secret_s=([0-9.]+) secret_frac=([0-9.]+)% "
                r"signs=(\d+) toggles=(\d+)")

with open(out, "w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["rep", "host", "arm", "total_s", "secret_s", "toggles", "checksum"])
    for r in range(reps + burnin):
        keep = r >= burnin
        for name, mk in HOSTS:
            order = ARMS[r % len(ARMS):] + ARMS[:r % len(ARMS)]
            for arm, variant, mode in order:
                argv, extra = mk(variant, mode)
                env = dict(os.environ)
                env.update(extra)
                p = subprocess.run(argv, cwd=SEC, env=env, capture_output=True, text=True)
                if p.returncode != 0:
                    print(f"  !! {name}/{arm} rc={p.returncode} {p.stderr[:150]}", flush=True)
                    continue
                m = rx.search(p.stdout)
                ck = ""
                for line in p.stdout.split("\n"):
                    if line.startswith("WORK"):
                        ck = line.strip()
                if m and keep:
                    w.writerow([r - burnin, name, arm, m.group(1), m.group(2), m.group(5), ck])
            fh.flush()
        print(f"rep {r - burnin if keep else 'burnin'} done", flush=True)
print("WROTE", out)
