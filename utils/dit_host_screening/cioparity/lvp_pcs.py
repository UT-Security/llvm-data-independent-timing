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
nm = os.path.join(os.environ.get("LLVM_BUILD", os.path.join(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), "build")), "bin", "llvm-nm")
blocks = open(f"{d}/stats.txt").read().split("---------- Begin Simulation Statistics ----------")[1:]
# The window is the ROIs THEMSELVES, not the tick span from the first to the
# last. Each dump covers [finalTick - simTicks, finalTick]; the gap before it is
# driver code between m5_dump_reset_stats and the next m5_reset_stats -- keygen,
# message setup, the driver's own crypto_verify_16 correctness check -- and on
# these runs that gap is about HALF the span. Counting it inverted the
# aes256gcm finding: encrypt reported 212 crypto_verify_16 predictions that were
# entirely the driver's, against 0 inside its ROI.
wins = []
for b in blocks:
    ft = re.search(r"^finalTick\s+(\d+)", b, re.M)
    st = re.search(r"^simTicks\s+(\d+)", b, re.M)
    if ft and st:
        wins.append((int(ft.group(1)) - int(st.group(1)), int(ft.group(1))))
wins = wins[warm:warm + iters]
if not wins:
    sys.exit(f"no ROI dumps: stats.txt has {len(blocks)}, need > warm={warm}")
starts = [a for a, _ in wins]
def in_roi(t):
    i = bisect.bisect_right(starts, t) - 1
    return i >= 0 and t <= wins[i][1]
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
    if not m or m.group(4) != "load" or not in_roi(int(m.group(1))): continue
    pc = int(m.group(3), 16); kind[pc] = m.group(5)
    (ok if m.group(2) == "correct" else bad)[pc] += 1
print(f"correct load predictions in the window: {sum(ok.values())} ({sum(ok.values())/iters:.0f}/op), incorrect {sum(bad.values())}, distinct PCs {len(ok)}")
for pc, c in ok.most_common(30):
    print(f"  {pc:#x}  {c:5d}  {kind[pc]:6s}  {sym(pc)}")
