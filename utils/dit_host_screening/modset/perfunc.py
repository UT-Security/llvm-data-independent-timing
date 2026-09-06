#!/usr/bin/env python3
"""Count MSR DIT switches per function in an object file.

The whole-file count hides the thing that matters: WHICH functions carry
switches. A drop in the total could be a precision win or a coverage loss, and
only the per-function breakdown separates them.
"""
import re, subprocess, sys, collections

import os, pathlib
OBJDUMP = str(pathlib.Path(os.environ.get("LLVM_BUILD",
    pathlib.Path(__file__).resolve().parents[3] / "build")) / "bin/llvm-objdump")

def counts(path):
    out = subprocess.run([OBJDUMP, "-d", path], capture_output=True, text=True).stdout
    cur, c = None, collections.Counter()
    for line in out.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.+)>:", line)
        if m:
            cur = m.group(1)
            c.setdefault(cur, 0)
            continue
        if cur and re.search(r"\bmsr\b.*\bdit\b", line, re.I):
            c[cur] += 1
    return c

if __name__ == "__main__":
    a, b = counts(sys.argv[1]), counts(sys.argv[2])
    la, lb = sys.argv[3], sys.argv[4]
    keys = sorted(set(a) | set(b), key=lambda k: (-max(a.get(k,0), b.get(k,0)), k))
    print(f"{'function':<52}{la:>10}{lb:>10}{'delta':>8}")
    ta = tb = 0
    for k in keys:
        x, y = a.get(k, 0), b.get(k, 0)
        ta += x; tb += y
        if x or y:
            print(f"{k[:52]:<52}{x:>10}{y:>10}{y-x:>8}")
    print(f"{'TOTAL':<52}{ta:>10}{tb:>10}{tb-ta:>8}")
    fa = sum(1 for k in a if a[k]); fb = sum(1 for k in b if b[k])
    print(f"{'functions carrying >=1 switch':<52}{fa:>10}{fb:>10}{fb-fa:>8}")
