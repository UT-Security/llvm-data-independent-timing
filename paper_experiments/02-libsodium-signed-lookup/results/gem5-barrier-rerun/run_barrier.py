#!/usr/bin/env python3
"""Focused gem5 rerun: what the CORRECTED barrier does to exp02's bracket column.

Same invocation run_gem5.py uses (--eves --dmp --comp-simp, plus
--no-speculative-dit for the serialising model), the same driver, the same
unhardened libsodium, offset 0. Three arms:

  base     unhardened baseline
  api      Apple's bracket -- NOW `dsb nsh; isb sy`, api_bracket.c's new default,
           where the committed gem5_arms.csv had `isb sy` alone
  apinop   its instruction-matched twin (18 instructions now, not 17)

api - apinop is the bracket's own cost with layout removed, which is the number
the committed CSV puts at 31-58 cycles/request and the M4 at 460-580.
"""
import concurrent.futures as cf, hashlib, os, pathlib, re, statistics, subprocess, sys, time

S = pathlib.Path(__file__).resolve().parent
G5 = pathlib.Path.home() / "Documents/gem5-DIT"
CFG = G5 / "configs/example/arm/fdp_neoverse_v2_binary.py"
GEM5 = G5 / "build/ARM/gem5.fast"
BASEFLAGS = ["--eves", "--dmp", "--comp-simp"]
MODELS = {"renamed": [], "serialising": ["--no-speculative-dit"]}
POINTS = [(10, 1600), (50, 1500), (200, 1270), (1000, 670), (5000, 200), (20000, 55)]
ARMS = ["base", "api", "apinop"]
WARMUP, Q = 50, 3
# FIVE STACK OFFSETS, as the rig does. gem5 SE puts argv[0] on the initial
# stack, so its length shifts alignment for the whole run, and the committed
# gem5_arms.csv is a median over five argv[0] lengths for exactly that reason.
# At one offset the bracket's own cost (api - apinop) came out NEGATIVE at 11 of
# 12 cells -- it is smaller than the layout term, so the layout term has to be
# averaged out before the number means anything.
OFFSETS = [0, 1, 2, 3, 4]


def canon(src, key, off):
    """A fixed-width binary path, `off` bytes longer, as run_gem5.py's canon()."""
    root = pathlib.Path("/tmp") / ("slbar_" + hashlib.md5(str(S).encode()).hexdigest()[:12])
    c = root / hashlib.md5(key.encode()).hexdigest()[:8] / ("b" + "x" * off)
    c.parent.mkdir(parents=True, exist_ok=True)
    if c.exists():
        c.unlink()
    try:
        os.link(src, c)
    except OSError:
        import shutil
        shutil.copy2(src, c)
    return c


def dumps(path):
    blocks, cur = [], None
    for line in open(path):
        if line.startswith("---------- Begin"):
            cur = {}
        elif line.startswith("---------- End"):
            if cur is not None:
                blocks.append(cur)
            cur = None
        elif cur is not None:
            p = line.split()
            if len(p) >= 2:
                try:
                    cur[p[0]] = float(p[1])
                except ValueError:
                    pass
    return blocks


def pick(b, *names):
    for n in names:
        for k in b:
            if k.endswith(n):
                return b[k]
    return None


def one(arm, model, L, iters, off):
    key = f"L{L}_{arm}_{model}"
    d = S / "runs" / (key + (f"_o{off}" if off else ""))
    d.mkdir(parents=True, exist_ok=True)
    if not (d / "stats.txt").exists():
        cmd = [str(GEM5), f"--outdir={d}", str(CFG),
               "--binary", str(canon(S / "bin" / f"gem5_{arm}", key, off)),
               "--arguments", f"--iter {iters} --warmup {WARMUP} --lookups {L} --predictable {Q}"
               ] + BASEFLAGS + MODELS[model]
        t0 = time.time()
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=str(d))
        (d / "run.log").write_text(p.stdout + p.stderr)
        (d / "cmd.txt").write_text(" ".join(cmd) + "\n")
    b = dumps(d / "stats.txt") if (d / "stats.txt").exists() else []
    if not b:
        return (arm, model, L, off, None)
    return (arm, model, L, off, {
        "cycles": pick(b[0], "core.numCycles", "numCycles"),
        "insts": pick(b[0], "commitStats0.numInsts", "commit.committedInsts",
                      "committedInsts"),
        "ditw": pick(b[0], "commit.ditWrites") or 0.0,
        "dumps": len(b),
    })


jobs = [(a, m, L, it, o) for L, it in POINTS for a in ARMS
        for m in ("renamed", "serialising") for o in OFFSETS]
print(f"{len(jobs)} gem5 runs (5 stack offsets), 9 at a time", flush=True)
res, done = {}, 0
with cf.ThreadPoolExecutor(max_workers=9) as ex:
    for arm, model, L, off, r in ex.map(lambda j: one(*j), jobs):
        res.setdefault((arm, model, L), {})[off] = r
        done += 1
        if done % 30 == 0:
            print(f"  {done}/{len(jobs)}", flush=True)

med = lambda arm, m, L, k: statistics.median(
    [v[k] for v in res[(arm, m, L)].values() if v]) if res.get((arm, m, L)) else None
spread = lambda arm, m, L: (lambda xs: (max(xs) - min(xs)) / statistics.median(xs) * 100)(
    [v["cycles"] for v in res[(arm, m, L)].values() if v])

print("\n=== exp02 gem5 arms, barrier = dsb nsh + isb sy (api_bracket.c's new default) ===")
print("median of 5 stack offsets, offset-0 binary hard-linked at 5 argv[0] lengths\n")
print(f"{'L':>7} {'req':>5} {'model':>12} {'base c/req':>11} {'api-apinop':>11} {'% req':>7} "
      f"{'ser-ren':>8} {'cyc/write':>10} {'spread':>7} {'ditW/req':>9}")
for L, it in POINTS:
    for m in ("serialising", "renamed"):
        bc = med("base", m, L, "cycles"); ac = med("api", m, L, "cycles")
        nc = med("apinop", m, L, "cycles"); dw = med("api", m, L, "ditw")
        if None in (bc, ac, nc):
            print(f"{L:>7} {it:>5} {m:>12}  (incomplete)"); continue
        d = (ac - nc) / it
        sr = ((med("api", "serialising", L, "cycles") - med("api", "renamed", L, "cycles")) / it)
        print(f"{L:>7} {it:>5} {m:>12} {bc/it:>11,.0f} {d:>11,.0f} {d/(bc/it)*100:>6.2f}% "
              f"{sr:>8,.1f} {sr/2:>10,.1f} {spread('api', m, L):>6.2f}% {dw/it:>9.1f}")
print("\nser-ren is the SAME BINARY under the two switch models, so it carries no")
print("layout term at all: it is the only clean number here, and /2 is per write.")
