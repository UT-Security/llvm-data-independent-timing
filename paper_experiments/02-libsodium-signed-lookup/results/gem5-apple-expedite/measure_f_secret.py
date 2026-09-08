#!/usr/bin/env python3
"""f_secret for the gem5 lane, measured the way the rig measures it.

The main sweep did not run --nosecret, so the panel would otherwise have to
borrow the M4's secret fraction -- and paper_experiments/02's known limits say
plainly that the two instruments sit at different points on the f axis and
cannot be compared until one is swept to match the other. So measure it here:
the public lane alone, same binary, same 5 stack offsets, f = (full - pub)/full
on the unhardened arm.
"""
import concurrent.futures as cf, hashlib, os, pathlib, statistics, subprocess

S = pathlib.Path(__file__).resolve().parent
G5 = pathlib.Path.home() / "Documents/gem5-DIT"
CFG = G5 / "configs/example/arm/fdp_neoverse_v2_binary.py"
GEM5 = G5 / "build/ARM/gem5.fast"
POINTS = [(10, 1600), (50, 1500), (200, 1270), (1000, 670), (5000, 200), (20000, 55)]
OFFSETS = [0, 1, 2, 3, 4]


def dumps(p):
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
    return blk


def canon(src, key, off):
    root = pathlib.Path("/tmp") / ("slbar_" + hashlib.md5(str(S).encode()).hexdigest()[:12])
    c = root / hashlib.md5(key.encode()).hexdigest()[:8] / ("b" + "x" * off)
    c.parent.mkdir(parents=True, exist_ok=True)
    if c.exists(): c.unlink()
    try: os.link(src, c)
    except OSError:
        import shutil; shutil.copy2(src, c)
    return c


def one(L, iters, off):
    key = f"L{L}_base_nosec"
    d = S / "runs" / (key + (f"_o{off}" if off else ""))
    d.mkdir(parents=True, exist_ok=True)
    if not (d / "stats.txt").exists():
        cmd = [str(GEM5), f"--outdir={d}", str(CFG),
               "--binary", str(canon(S / "bin" / "gem5_base", key, off)),
               "--arguments", f"--iter {iters} --warmup 50 --lookups {L} --predictable 3 --nosecret",
               "--eves", "--dmp", "--comp-simp", "--apple"]
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=str(d))
        (d / "run.log").write_text(p.stdout + p.stderr)
    b = dumps(d / "stats.txt") if (d / "stats.txt").exists() else []
    if not b: return (L, off, None)
    for k in b[0]:
        if k.endswith("core.numCycles"): return (L, off, b[0][k])
    return (L, off, None)


jobs = [(L, it, o) for L, it in POINTS for o in OFFSETS]
res = {}
with cf.ThreadPoolExecutor(max_workers=6) as ex:
    for L, off, c in ex.map(lambda j: one(*j), jobs):
        res.setdefault(L, []).append(c)

# the full-flow base cycles, from the main sweep's run dirs
def full(L):
    v = []
    for off in OFFSETS:
        d = S / "runs" / (f"L{L}_base_apple" + (f"_o{off}" if off else ""))
        b = dumps(d / "stats.txt") if (d / "stats.txt").exists() else []
        if b:
            for k in b[0]:
                if k.endswith("core.numCycles"): v.append(b[0][k]); break
    return statistics.median(v) if v else None

print("gem5's own secret fraction, median of 5 offsets")
print(f"{'L':>7} {'full cyc':>12} {'public-only':>12} {'f_secret':>9}")
out = {}
for L, it in POINTS:
    f_, p_ = full(L), statistics.median([c for c in res[L] if c])
    fs = (f_ - p_) / f_ * 100
    out[L] = fs
    print(f"{L:>7} {f_:>12,.0f} {p_:>12,.0f} {fs:>8.2f}%")
import json
(S / "gem5_f_secret.json").write_text(json.dumps(out))
print(f"\nwrote {S / 'gem5_f_secret.json'}")
