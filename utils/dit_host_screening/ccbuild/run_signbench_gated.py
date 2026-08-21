#!/usr/bin/env python3
"""Timing: always-on vs pass-placed DIT on a real web3.py signing workload.

ARMS
  baseline    coincurve built -ftaint-harden=<empty>  (0 switches)   no dylib
  null        same binary + dit_off.dylib injected    -> harness cost
  always      same binary + dit_on.dylib injected     -> blanket DIT
  oracle      vendored secp256k1 patched: DIT on at ecdsa_sign entry,
              off at exit. Exactly 2 switches/signature, built with the EMPTY
              seed so the pass adds nothing. The upper bound on placement.
  pass_region coincurve built with the real seed      (594 switches) no dylib
  pass_hoist  + -taint-dit-loop-hoist=1               (575 switches) no dylib
  pass_relaxed + -taint-dit-relaxed-ownership        (289 switches) no dylib
  pass_gated  + -taint-modset-callsite-gated          (39 switches)  no dylib
  pass_clonegated  clone list + the gate              (34 switches)  no dylib

The gate is the point of this run. 93.6% of this library's switches were false
positives (dit-coincurve-real-application.md); the gate removes them, taking the
build from 575 switches to 39 against the hand oracle's 8. The prior verdict here
- pass +2.74% WORSE than blanket always-on, prize only 0.64% - predates it.
  baseline2   a second run of baseline                -> the rig's noise floor

`always` is compared to `null`, NOT to `baseline`, so the cost of injecting the
dylib is not charged to DIT. The pass arms are compared to `baseline`; all three
coincurve builds went through the same MIR round-trip, so the codegen lottery
(dit-measurement-traps trap 7) is controlled for.

Arm order is rotated every rep - a fixed order penalises whichever arm runs last
by ~1% (see dit-oracle-composites.md).

The lvp_chase control runs in-band every rep (trap 5): if DIT stops taking
effect mid-sweep, a null result would otherwise be indistinguishable from a
broken rig.
"""
import csv
import os
import re
import subprocess
import sys
import time

CC = os.path.dirname(os.path.abspath(__file__))
SW = os.path.dirname(CC)
DIT_ON = os.path.join(SW, "sweep", "bin", "dit_on.dylib")
DIT_OFF = os.path.join(SW, "sweep", "bin", "dit_off.dylib")
PROBE = os.path.join(SW, "sweep", "bin", "dit_probe")

N = os.environ.get("SIGN_N", "25000")

ARMS = [
    ("baseline",        "nodit4",       None),
    ("null",            "nodit4",       DIT_OFF),
    ("always",          "nodit4",       DIT_ON),
    ("oracle",          "oracle4",      None),
    ("pass_hoist",      "hoist4",       None),
    ("pass_clone",      "clone4",       None),
    ("pass_gated",      "gated4",       None),
    ("pass_clonegated", "clonegated4",  None),
    ("baseline2",       "nodit4",       None),
]

reps = int(sys.argv[1]) if len(sys.argv) > 1 else 50
burnin = int(sys.argv[2]) if len(sys.argv) > 2 else 3
out = os.path.join(CC, "signbench_gated.csv")

rx = re.compile(r"total_s=([0-9.]+) raw_sign_s=([0-9.]+) secret_frac=([0-9.]+)% checksum=(\d+)")
prx = re.compile(r"const_ns_per_hop=([0-9.]+) perm_ns_per_hop=([0-9.]+)")


def run(argv, dylib):
    env = dict(os.environ)
    env.pop("DYLD_INSERT_LIBRARIES", None)
    if dylib:
        env["DYLD_INSERT_LIBRARIES"] = dylib
    return subprocess.run(argv, cwd=CC, env=env, capture_output=True, text=True)


with open(out, "w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["rep", "arm", "total_s", "raw_sign_s", "secret_frac", "checksum",
                "probe_const_off", "probe_const_on"])
    for r in range(reps + burnin):
        keep = r >= burnin

        # in-band control, same injection path the `always` arm uses
        poff = prx.search(run([PROBE, "20000000"], DIT_OFF).stdout)
        pon = prx.search(run([PROBE, "20000000"], DIT_ON).stdout)
        c_off = poff.group(1) if poff else ""
        c_on = pon.group(1) if pon else ""

        order = ARMS[r % len(ARMS):] + ARMS[:r % len(ARMS)]
        for arm, variant, dylib in order:
            py = os.path.join(CC, f"venv_{variant}", "bin", "python")
            p = run([py, "sign_workload.py", N], dylib)
            if p.returncode != 0:
                print(f"  !! {arm} rc={p.returncode} {p.stderr[:160]}", flush=True)
                continue
            m = rx.search(p.stdout)
            if m and keep:
                w.writerow([r - burnin, arm, m.group(1), m.group(2), m.group(3),
                            m.group(4), c_off, c_on])
        fh.flush()
        print(f"rep {r - burnin if keep else 'burnin'} done", flush=True)

print("WROTE", out)
