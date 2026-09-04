#!/usr/bin/env python3
"""Map UNDERTAINT pcs in a gem5 .err to functions via llvm-nm (robust to symbolizer quirks).
usage: symfn.py <binary> <err file> [top]"""
import bisect, re, subprocess, sys
from collections import defaultdict
binp, errp = sys.argv[1], sys.argv[2]; top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
nm = subprocess.run(['/home/rgangar/Documents/llvm-data-independent-timing/build/bin/llvm-nm', '-n', '--defined-only', binp], capture_output=True, text=True).stdout
syms = []
for line in nm.splitlines():
    p = line.split()
    if len(p) == 3 and p[1] in 'tTwW':
        syms.append((int(p[0], 16), p[2]))
addrs = [a for a, _ in syms]
agg = defaultdict(lambda: [0, 0])
for pc, cnt in re.findall(r'^  UNDERTAINT pc=(0x[0-9a-f]+) count=(\d+)', open(errp, errors='replace').read(), re.M):
    i = bisect.bisect_right(addrs, int(pc, 16)) - 1
    f = syms[i][1] if i >= 0 else '?'
    agg[f][0] += int(cnt); agg[f][1] += 1
for f, (c, n) in sorted(agg.items(), key=lambda kv: -kv[1][0])[:top]:
    print(f"  {c:9d} ops {n:4d} sites  {f}")
