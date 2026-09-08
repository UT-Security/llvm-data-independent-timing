#!/usr/bin/env python3
"""Where does a flush-after switch's cost peak, as a fraction of the request?

Under --apple a mode write squashes at commit, so its cost is what the machine
had run ahead into and has to re-run. At the AEAD entry with a 100-byte message
there is almost nothing in flight (the public lane ahead is a serial chase by
construction) and the bracket costs 42 cycles of a 2,481-cycle request. Give the
crypto more to do and the machine runs further ahead, so the entry switch
discards more -- until the crypto outgrows the window and the fixed drain is
diluted again. Sweep the message size and find the peak.
"""
import concurrent.futures as cf, pathlib, statistics, subprocess

S = pathlib.Path(__file__).resolve().parent
G5 = pathlib.Path.home() / "Documents/gem5-DIT"
CFG = G5 / "configs/example/arm/fdp_neoverse_v2_binary.py"
GEM5 = G5 / "build/ARM/gem5.fast"
FLAGS = ["--eves", "--dmp", "--comp-simp", "--apple"]
L = 200
# iter tuned so each ROI is a few million cycles: the AEAD dominates once the
# message is past a kilobyte, so cycles/request grows roughly with it.
SIZES = [(16, 900), (64, 800), (256, 600), (1024, 350)]
ARMS = ["base", "blanket", "api", "apinop"]


def blk0(p):
    blk, cur = [], None
    for line in open(p):
        if line.startswith("---------- Begin"):
            cur = {}
        elif line.startswith("---------- End"):
            if cur is not None:
                blk.append(cur); cur = None
        elif cur is not None:
            q = line.split()
            if len(q) >= 2:
                try: cur[q[0]] = float(q[1])
                except ValueError: pass
    return blk[0] if blk else None


def one(arm, mb, it):
    d = S / "runs" / f"{arm}_{mb}"
    d.mkdir(parents=True, exist_ok=True)
    if not (d / "stats.txt").exists():
        subprocess.run([str(GEM5), f"--outdir={d}", str(CFG),
                        "--binary", str(S / "bin" / f"{arm}_{mb}"),
                        "--arguments",
                        f"--iter {it} --warmup 20 --lookups {L} --predictable 3"]
                       + FLAGS, capture_output=True, text=True, cwd=str(d))
    b = blk0(d / "stats.txt") if (d / "stats.txt").exists() else None
    if not b:
        return (arm, mb, None)
    g = lambda k: next((b[x] for x in b if x.endswith(k)), None)
    return (arm, mb, {"cyc": g("core.numCycles"), "ins": g("commitStats0.numInsts"),
                      "ditw": g("commit.ditWrites") or 0})


jobs = [(a, mb, it) for mb, it in SIZES for a in ARMS]
res = {}
with cf.ThreadPoolExecutor(max_workers=5) as ex:
    for arm, mb, r in ex.map(lambda j: one(*j), jobs):
        res[(arm, mb)] = r

print(f"AES-256-GCM secret lane, L={L}, --apple, one bracketed call per request\n")
print(f"{'secret B':>9} {'iter':>5} {'base c/req':>11} {'IPC':>6} {'blanket':>9} "
      f"{'api-twin':>9} {'% of req':>9} {'ditW/req':>9}")
for mb, it in SIZES:
    b, bl, a, n = (res[("base", mb)], res[("blanket", mb)],
                   res[("api", mb)], res[("apinop", mb)])
    if not all((b, bl, a, n)):
        print(f"{mb:>9} {it:>5}   (failed)"); continue
    br = b["cyc"] / it
    d = (a["cyc"] - n["cyc"]) / it
    blk = (bl["ins"]/bl["cyc"])
    ovb = ((b["ins"]/b["cyc"]) / blk - 1) * 100
    print(f"{mb:>9} {it:>5} {br:>11,.0f} {b['ins']/b['cyc']:>6.2f} {ovb:>+8.2f}% "
          f"{d:>9,.0f} {d/br*100:>8.2f}% {a['ditw']/it:>9.1f}")
