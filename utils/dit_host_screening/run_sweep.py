#!/usr/bin/env python3
"""Always-on DIT screening sweep across candidate host applications.

DESIGN (per dit-measurement-traps):
 - SAME BINARY in all three arms. The taint pipeline is never involved, so the
   MIR round-trip codegen lottery (trap 7b) cannot apply at all. DIT is set by
   DYLD_INSERT_LIBRARIES at process load.
 - THREE arms: base (no dylib), null (dylib present, DIT never set), dit (dylib
   sets DIT on every thread). base->null is the harness cost, null->dit is the
   DIT cost. The null arm is not optional: it cost +0.30% on Chromium.
 - CONTROL IN-BAND (trap 5): dit_probe runs every rep under the same injection.
   Its const chase must read ~4x between null and dit or the sweep measured
   nothing. dit_probe never touches DIT itself, so it validates the entire
   injection path exactly as each host experiences it.
 - PAIRED ROUND-ROBIN (trap 3): arms for a given host run adjacently, hosts
   rotate within a rep, burn-in discarded.

Writes one CSV row per (rep, host, arm).
"""
import csv
import os
import subprocess
import sys
import time

SW = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(SW, "bin")
SRC = os.path.join(SW, "src")
HOME = os.path.expanduser("~")

DIT_ON = os.path.join(BIN, "dit_on.dylib")
DIT_OFF = os.path.join(BIN, "dit_off.dylib")

HOSTS = [
    # (name, argv, cwd)
    ("probe",   [os.path.join(BIN, "dit_probe"), "20000000"], SW),
    # --testset main = the core CRUD/index workload (VDBE dispatch + B-tree
    # descent). The "app" testset needs an on-disk DB and errors on :memory:.
    ("sqlite",  [os.path.join(BIN, "speedtest1"), "--size", "300", "--nosync",
                 "--testset", "main", ":memory:"], SW),
    ("lua",     [os.path.join(SRC, "lua-5.4.7", "src", "lua"), "bench.lua", "17"],
                os.path.join(SW, "lua")),
    ("cpython", [os.path.join(SRC, "cpython", "python.exe"), "driver.py", "all"],
                os.path.join(SW, "py")),
    ("git",     ["/opt/homebrew/bin/git", "rev-list", "--all", "--count"],
                os.path.join(HOME, "Documents", "llvm-project")),
    ("quickjs", [os.path.join(HOME, "Documents", "quickjs-2025-04-26", "qjs"), "bench/all.js"],
                os.path.join(HOME, "Documents", "quickjs-2025-04-26")),
]

ARMS = [("base", None), ("null", DIT_OFF), ("dit", DIT_ON)]


def run_one(argv, cwd, dylib):
    env = dict(os.environ)
    env.pop("DYLD_INSERT_LIBRARIES", None)
    if dylib:
        env["DYLD_INSERT_LIBRARIES"] = dylib
    t0 = time.perf_counter()
    p = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, text=True)
    t1 = time.perf_counter()
    return t1 - t0, p.returncode, p.stdout.strip(), p.stderr.strip()


def main():
    reps = int(sys.argv[1]) if len(sys.argv) > 1 else 12
    burnin = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    out = os.path.join(SW, "out", "sweep.csv")

    with open(out, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["rep", "host", "arm", "seconds", "rc", "stdout_tail", "probe_const", "probe_perm", "probe_dit"])

        for r in range(reps + burnin):
            keep = r >= burnin
            for name, argv, cwd in HOSTS:
                for arm, dylib in ARMS:
                    secs, rc, so, se = run_one(argv, cwd, dylib)
                    pc = pp = pd = ""
                    if name == "probe":
                        for line in so.split("\n"):
                            if line.startswith("PROBE"):
                                kv = dict(t.split("=") for t in line.split()[1:])
                                pc, pp, pd = kv.get("const_ns_per_hop", ""), kv.get("perm_ns_per_hop", ""), kv.get("dit", "")
                    if rc != 0:
                        print(f"  !! {name}/{arm} rc={rc} {se[:200]}", flush=True)
                    if keep:
                        tail = so.split("\n")[-1][:120] if so else ""
                        w.writerow([r - burnin, name, arm, f"{secs:.6f}", rc, tail, pc, pp, pd])
                fh.flush()
            print(f"rep {r - burnin if keep else 'burnin'} done", flush=True)

    print("WROTE", out)


if __name__ == "__main__":
    main()
