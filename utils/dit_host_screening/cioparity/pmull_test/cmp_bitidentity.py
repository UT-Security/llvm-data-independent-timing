#!/usr/bin/env python3
"""Bit-identity check: same binaries, same paths, only the simulator differs.

WHY IT IS DONE THIS WAY. The obvious comparison -- our numbers against the
parent sweep's recorded ones -- cannot work. canon() hard-links every arm to a
fixed-WIDTH path under $WORK, and our WORK ends in "-aes" against the parent's
"-wl": one character, which shifts the initial process stack and with it the
cycle count. That is the documented argv[0] trap, so a mismatch there would say
nothing about the patch.

Running the same binaries from the same WORK under the unpatched and patched
simulators isolates exactly one variable. chacha20 executes no PMULL at all, so
any difference is the patch touching code it had no business touching.
"""
import json
import sys


def load(path):
    with open(path) as fh:
        rows = (json.loads(line) for line in fh if line.strip())
        return {(r["arm"], r["cfg"]): r for r in rows if not r.get("error")}


def main():
    base = "/home/rgangar/Documents/libsodium-cioparity-aes"
    a = load(base + "/out_ctl_unpatched/results.jsonl")
    b = load(base + "/out_ctl_patched/results.jsonl")
    bad = 0
    print("%-22s%15s%14s%12s%7s" % ("cell", "unpatched cyc", "patched cyc",
                                    "insts same", "match"))
    for k in sorted(a):
        if k not in b:
            continue
        x, y = a[k], b[k]
        same = (x["cycles_total"] == y["cycles_total"]
                and x["insts_total"] == y["insts_total"])
        if not same:
            bad += 1
        print("%-22s%15.0f%14.0f%12s%7s" % (
            k[0] + "/" + k[1], x["cycles_total"], y["cycles_total"],
            x["insts_total"] == y["insts_total"], "YES" if same else "NO"))
    print()
    if bad:
        print("%d cell(s) DIFFER -- the patch has side effects" % bad)
        return 1
    print("BIT-IDENTICAL on every cell -- the patch does not perturb "
          "non-PMULL code")
    return 0


if __name__ == "__main__":
    sys.exit(main())
