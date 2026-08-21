#!/usr/bin/env python3
"""The decisive experiment: does ORACLE placement beat ALWAYS-ON?

One binary per host, DIT mode chosen at RUNTIME (argv), so no codegen differs
between arms - dit-measurement-traps trap 7b cannot apply. Three modes:

  off     never set DIT                    (the baseline)
  always  set once before work             (the always-on reference)
  oracle  set/cleared around each signature (perfect fine-grained placement)

The oracle is the UPPER BOUND on any placement, including anything the taint
pass could produce. If oracle does not beat always-on here, no pass can.

COVERAGE (trap 8): the secret key is touched only inside secret_sign_n between
the enable and disable, so the oracle protects 100% of secret work by
construction. An under-protecting oracle looks exactly like a win, which is why
this is stated structurally rather than by inspection.
"""
import csv
import os
import re
import statistics as st
import subprocess
import sys
import time

SEC = os.path.dirname(os.path.abspath(__file__))
SW = os.path.dirname(SEC)
CP = os.path.join(SW, "src", "cpython")

ENV_CPY = {"PYTHONHOME": CP,
           "PYTHONPATH": os.path.join(CP, "Lib") + ":" +
                         os.path.join(CP, "build", "lib.macosx-26.6-arm64-3.16")}

HOSTS = [
    ("lua",     lambda m: ([os.path.join(SEC, "host_lua"), "work.lua", str(m), "17", "1"], {})),
    ("sqlite",  lambda m: ([os.path.join(SEC, "host_sqlite"), str(m), "120000", "14", "1"], {})),
    ("cpython", lambda m: ([os.path.join(SEC, "host_cpython"), "work.py", str(m), "1"], ENV_CPY)),
]
if os.path.exists(os.path.join(SEC, "host_quickjs")):
    HOSTS.append(("quickjs", lambda m: ([os.path.join(SEC, "host_quickjs"), "work.js", str(m)], {})))

# `off2` is a SECOND run of the identical off arm. off vs off2 is the rig's own
# noise floor: same binary, same mode, so any difference is ordering/thermal
# drift, not DIT. Added after a zero-signature control showed the oracle arm
# reading +1.28% with ZERO toggles and ZERO secret work - i.e. a fixed arm order
# was systematically penalising whichever arm ran last.
MODES = [("off", 0), ("always", 1), ("oracle", 2), ("off2", 0)]

reps = int(sys.argv[1]) if len(sys.argv) > 1 else 15
burnin = int(sys.argv[2]) if len(sys.argv) > 2 else 2
out = os.path.join(SW, "out", "oracle.csv")

rx = re.compile(r"total_s=([0-9.]+) secret_s=([0-9.]+) secret_frac=([0-9.]+)% "
                r"signs=(\d+) toggles=(\d+)")

with open(out, "w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["rep", "host", "mode", "total_s", "secret_s", "secret_frac",
                "signs", "toggles", "checksum"])
    for r in range(reps + burnin):
        keep = r >= burnin
        for name, mk in HOSTS:
            # ROTATE arm order every rep so no arm is permanently last.
            order = MODES[r % len(MODES):] + MODES[:r % len(MODES)]
            for mode, mv in order:
                argv, extra = mk(mv)
                env = dict(os.environ)
                env.update(extra)
                p = subprocess.run(argv, cwd=SEC, env=env, capture_output=True, text=True)
                if p.returncode != 0:
                    print(f"  !! {name}/{mode} rc={p.returncode} {p.stderr[:160]}", flush=True)
                    continue
                m = rx.search(p.stdout)
                ck = ""
                for line in p.stdout.split("\n"):
                    if line.startswith("WORK"):
                        ck = line.strip()
                if m and keep:
                    w.writerow([r - burnin, name, mode, m.group(1), m.group(2),
                                m.group(3), m.group(4), m.group(5), ck])
            fh.flush()
        print(f"rep {r - burnin if keep else 'burnin'} done", flush=True)

print("WROTE", out)
