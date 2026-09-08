#!/usr/bin/env python3
"""The gem5 crossover on a secret lane gem5 can resolve: AES-256-GCM, 64 bytes.

Same public lane, same L sweep, same arms. What changed is the secret op.
chacha20-poly1305 is a serial ARX chain and does not fill the reorder window
until the message is ~4 KB, by which point the request is 46,000 cycles and a
~300-cycle flush-after switch is 0.6% of it -- under the model's own layout
noise. AES-GCM has independent round and GHASH work per block, so it saturates
the window at 16-64 bytes: the bracket costs 285-364 cycles there, which is the
same ballpark as the 455-510 measured on an M4, against a ~2,000-cycle request.

The `sb` must sit immediately behind the `msr DIT, #1` for the renamed switch to
fuse it, so api_bracket.c emits the two from one asm block. rename.ditBarrierFused
is carried into the CSV as the check: zero there means the barrier kept its
ordering and the --expedite curve is not measuring the renamed switch.
"""
import concurrent.futures as cf, csv, hashlib, os, pathlib, statistics, subprocess

S = pathlib.Path(__file__).resolve().parent
G5 = pathlib.Path.home() / "Documents/gem5-DIT"
CFG = G5 / "configs/example/arm/fdp_neoverse_v2_binary.py"
GEM5 = G5 / "build/ARM/gem5.fast"
BASEFLAGS = ["--eves", "--dmp", "--comp-simp"]
MODELS = {"apple": ["--apple"], "expedite": ["--expedite"]}
MB = 64
ARMS = ["base", "blanket", "api", "apinop"]
OFFSETS = [0, 1, 2, 3, 4]
# (L, iter): iter keeps every ROI a few million cycles. cycles/request runs from
# ~2,000 at L=10 to ~250,000 at L=20,000.
POINTS = [(10, 1500), (30, 1400), (50, 1300), (100, 1000), (200, 700),
          (1000, 200), (5000, 50), (20000, 14)]


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


def canon(src, key, off):
    root = pathlib.Path("/tmp") / ("slaes_" + hashlib.md5(str(S).encode()).hexdigest()[:12])
    c = root / hashlib.md5(key.encode()).hexdigest()[:8] / ("b" + "x" * off)
    c.parent.mkdir(parents=True, exist_ok=True)
    if c.exists(): c.unlink()
    try: os.link(src, c)
    except OSError:
        import shutil; shutil.copy2(src, c)
    return c


def one(arm, model, L, it, off, nosec=False):
    key = f"L{L}_{arm}_{model}" + ("_ns" if nosec else "")
    d = S / "runs" / (key + (f"_o{off}" if off else ""))
    d.mkdir(parents=True, exist_ok=True)
    if not (d / "stats.txt").exists():
        args = f"--iter {it} --warmup 20 --lookups {L} --predictable 3"
        if nosec: args += " --nosecret"
        subprocess.run([str(GEM5), f"--outdir={d}", str(CFG),
                        "--binary", str(canon(S / "bin" / f"{arm}_{MB}", key, off)),
                        "--arguments", args] + BASEFLAGS + MODELS[model],
                       capture_output=True, text=True, cwd=str(d))
    b = blk0(d / "stats.txt") if (d / "stats.txt").exists() else None
    g = lambda k: next((b[x] for x in b if x.endswith(k)), None) if b else None
    return (arm, model, L, off, nosec,
            None if not b else {"cyc": g("core.numCycles"),
                                "ins": g("commitStats0.numInsts"),
                                "ditw": g("commit.ditWrites") or 0.0,
                                "fused": g("rename.ditBarrierFused") or 0.0,
                                "ser": g("rename.serializing") or 0.0})


jobs = [(a, m, L, it, o, False) for L, it in POINTS for a in ARMS
        for m in ("apple", "expedite") for o in OFFSETS]
jobs += [("base", "apple", L, it, o, True) for L, it in POINTS for o in OFFSETS]
print(f"{len(jobs)} gem5 runs", flush=True)
res, done = {}, 0
with cf.ThreadPoolExecutor(max_workers=6) as ex:
    for arm, model, L, off, nosec, r in ex.map(lambda j: one(*j), jobs):
        res.setdefault((arm, model, L, nosec), {})[off] = r
        done += 1
        if done % 40 == 0: print(f"  {done}/{len(jobs)}", flush=True)

med = lambda a, m, L, k, ns=False: statistics.median(
    [v[k] for v in res[(a, m, L, ns)].values() if v])

rows = []
print(f"\nAES-256-GCM {MB} B secret lane, median of 5 stack offsets")
print(f"{'f_sec':>7} {'L':>7} {'base c/req':>11} {'IPC':>6} {'blanket':>9} "
      f"{'api@apple':>10} {'api@exped':>10} {'brk@apple':>9} {'brk@exped':>9} "
      f"{'fused':>6}")
for L, it in POINTS:
    bc, bi = med("base", "apple", L, "cyc"), med("base", "apple", L, "ins")
    pc = med("base", "apple", L, "cyc", True)
    f = (bc - pc) / bc * 100
    ov = lambda a, m: ((bi / bc) / (med(a, m, L, "ins") / med(a, m, L, "cyc")) - 1) * 100
    brk = lambda m: (med("api", m, L, "cyc") - med("apinop", m, L, "cyc")) / it
    print(f"{f:>6.1f}% {L:>7} {bc/it:>11,.0f} {bi/bc:>6.2f} {ov('blanket','apple'):>+8.2f}% "
          f"{ov('api','apple'):>+9.2f}% {ov('api','expedite'):>+9.2f}% "
          f"{brk('apple'):>9,.0f} {brk('expedite'):>9,.0f} "
          f"{med('api','expedite',L,'fused')/it:>6.2f}")
    for m in ("apple", "expedite"):
        for a in ARMS:
            rows.append({"L": L, "requests": it, "secret_bytes": MB,
                         "secret_op": "aes256gcm", "f_secret_pct": round(f, 2),
                         "model": m, "arm": a,
                         "cycles": round(med(a, m, L, "cyc")),
                         "insts": round(med(a, m, L, "ins")),
                         "ipc": round(med(a, m, L, "ins") / med(a, m, L, "cyc"), 4),
                         "cyc_per_request": round(med(a, m, L, "cyc") / it, 1),
                         "ipc_ovh_pct": round(ov(a, m), 2),
                         "dit_writes": round(med(a, m, L, "ditw")),
                         "ditw_per_req": round(med(a, m, L, "ditw") / it, 2),
                         "bracket_cyc_per_req": round(
                             (med(a, m, L, "cyc")
                              - med("apinop", m, L, "cyc")) / it, 1),
                         "barrier_fused": round(med(a, m, L, "fused")),
                         "rename_serializing": round(med(a, m, L, "ser")),
                         "n_offsets": len([v for v in res[(a, m, L, False)].values() if v])})
out = S / "gem5_aes_arms.csv"
with open(out, "w", newline="") as fh:
    w = csv.DictWriter(fh, fieldnames=list(rows[0].keys())); w.writeheader()
    for r in rows: w.writerow(r)
print(f"\nwrote {out} ({len(rows)} rows)")
