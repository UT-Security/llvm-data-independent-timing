#!/usr/bin/env python3
"""lvp_pcs.py <run dir> [warm iters]: which load PCs the value predictor got right
inside the measured window of a gem5 run made with --debug-flags=LVP
--debug-file=trace.txt (the EVES trace: gem5-DIT-pmull `ditcycles` branch,
"EVES traces each value prediction and validation under LVP"). The run dir
holds the binary as `b`; PCs are symbolised with llvm-nm ($LLVM_BUILD).

The measured window is dumps[warm : warm+iters] of stats.txt, as in
run_cio_gem5.py. Default warm=2 iters=2 (the trace runs use a short loop)."""
import bisect, collections, os, re, subprocess, sys
d = sys.argv[1]; warm = int(sys.argv[2]) if len(sys.argv) > 2 else 2; iters = int(sys.argv[3]) if len(sys.argv) > 3 else 2
nm = os.path.join(os.environ.get("LLVM_BUILD", os.path.expanduser("~/Documents/llvm-data-independent-timing/build")), "bin", "llvm-nm")
blocks = open(f"{d}/stats.txt").read().split("---------- Begin Simulation Statistics ----------")[1:]
ends = [int(re.search(r"^finalTick\s+(\d+)", b, re.M).group(1)) for b in blocks]
lo, hi = ends[warm - 1] if warm else 0, ends[warm + iters - 1]
syms = []
for l in subprocess.run([nm, "-n", f"{d}/b"], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3 and p[1] in "tTwW": syms.append((int(p[0], 16), p[2]))
addrs = [a for a, _ in syms]
def sym(pc):
    i = bisect.bisect_right(addrs, pc) - 1
    return f"{syms[i][1]}+{pc - syms[i][0]:#x}" if i >= 0 else "?"
ok, bad, kind = collections.Counter(), collections.Counter(), {}
for l in open(f"{d}/trace.txt", errors="replace"):
    m = re.match(r"^\s*(\d+): .*EVES validate (\w+) \[sn:\d+\] PC (0x[0-9a-f]+) (load|alu) (stride|vtage)", l)
    if not m or not (lo < int(m.group(1)) <= hi) or m.group(4) != "load": continue
    pc = int(m.group(3), 16); kind[pc] = m.group(5)
    (ok if m.group(2) == "correct" else bad)[pc] += 1
print(f"correct load predictions in the window: {sum(ok.values())} ({sum(ok.values())/iters:.0f}/op), incorrect {sum(bad.values())}, distinct PCs {len(ok)}")
for pc, c in ok.most_common(30):
    print(f"  {pc:#x}  {c:5d}  {kind[pc]:6s}  {sym(pc)}")
