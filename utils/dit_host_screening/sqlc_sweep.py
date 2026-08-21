#!/usr/bin/env python3
"""SQLCipher cache-size sweep: the secret fraction knob in a real application.

THE DESIGN. Public lane = SQLite B-tree descent and record decode over decrypted
pages. Secret lane = SQLCipher's per-page AES-256-CBC + HMAC. The knob is
`PRAGMA cache_size`, which changes ONLY how often a page must be decrypted - the
database, the B-tree shape, the query set and the public work per query are
identical at every point. A cache larger than the database means the ROI decrypts
nothing and is pure public work; a tiny cache means nearly every query decrypts.

WHY cache_size AND NOT DATABASE SIZE. Growing the database also deepens the
B-tree, so it would move the public work as well as the secret work and the two
effects could not be separated. cache_size moves exactly one variable.

f_secret IS MEASURED, NOT ASSUMED. The driver reports pager cache misses, and in
SQLCipher one miss is exactly one page read, hence exactly one decrypt + HMAC.
So `decrypts_per_query` is a direct count of secret-region entries.

ARMS (compare placement to `nodit`, NEVER to `plain` - the MIR round-trip shifts
every function's address and is worth a few percent on its own):
  plain    -O2, no instrumentation            } the pair that gives
  blanket  -O2 + DIT set before main          } always-on DIT cost
  nodit    full taint pipeline, DIT off       } the pair that gives
  hoist    placement + loop hoisting          } the pass's cost
"""
import argparse, json, os, pathlib, re, subprocess, sys, time

BIN = pathlib.Path.home() / "Documents/gem5-DIT/benchmarks/native/bin"
ARMS = {"plain": "q_native", "blanket": "q_native_dit",
        "nodit": "q_native_nodit", "hoist": "q_native_hoist"}
ROI_RE = re.compile(r"ROI_NS (\d+) DIT (\d)")
SUM_RE = re.compile(r"cache_size=(\S+) hits=(\d+) misses=(\d+) "
                    r"decrypts_per_query=([\d.]+) checksum=(\d+)")


def run_one(arm, cache, rows, queries, payload, warmup, dbdir, kdf):
    exe = BIN / ARMS[arm]
    env = dict(os.environ)
    env["CACHE_SIZE"] = str(cache)
    env["KDF_ITER"] = str(kdf)
    # One database per (rows, payload) shape, built once and reused: without
    # --reuse every run re-encrypts the whole database and setup dominates.
    env["QDB"] = str(dbdir / f"q_r{rows}_p{payload}.db")
    out = subprocess.run(
        [str(exe), "--rows", str(rows), "--queries", str(queries),
         "--payload", str(payload), "--warmup", str(warmup), "--reuse"],
        capture_output=True, text=True, env=env, timeout=600)
    text = out.stdout
    m_roi, m_sum = ROI_RE.search(text), SUM_RE.search(text)
    if not m_roi or not m_sum:
        return {"ok": False, "error": (out.stderr or text)[:300]}
    return {"ok": True,
            "roi_ns": int(m_roi.group(1)), "dit_on": int(m_roi.group(2)),
            "cache_size": m_sum.group(1),
            "hits": int(m_sum.group(2)), "misses": int(m_sum.group(3)),
            "decrypts_per_query": float(m_sum.group(4)),
            "checksum": int(m_sum.group(5))}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--caches", default="16,64,256,1024,2048,4096")
    ap.add_argument("--arms", default="plain,blanket,nodit,hoist")
    ap.add_argument("--reps", type=int, default=10)
    ap.add_argument("--rows", type=int, default=2000)
    ap.add_argument("--queries", type=int, default=2000)
    ap.add_argument("--payload", type=int, default=3500)
    ap.add_argument("--warmup", type=int, default=200)
    ap.add_argument("--kdf", type=int, default=4000)
    ap.add_argument("--out", default=str(pathlib.Path.home() /
                                         "Documents/dit-browser-bench/results-sqlc.jsonl"))
    ap.add_argument("--dbdir", default=str(pathlib.Path.home() /
                                           "Documents/dit-browser-bench/sqlcdb"))
    args = ap.parse_args()

    dbdir = pathlib.Path(args.dbdir)
    dbdir.mkdir(parents=True, exist_ok=True)
    out = pathlib.Path(args.out)
    if out.exists() and out.stat().st_size:
        out.rename(out.with_suffix(out.suffix + "." + time.strftime("%Y%m%d-%H%M%S") + ".bak"))

    caches = [int(c) for c in args.caches.split(",")]
    arms = args.arms.split(",")
    for a in arms:
        if not (BIN / ARMS[a]).exists():
            sys.exit(f"missing binary for arm {a}: {BIN / ARMS[a]}")

    print(f"SQLCipher cache sweep: {len(caches)} cache sizes x {len(arms)} arms "
          f"x {args.reps} reps = {len(caches) * len(arms) * args.reps} runs")

    # Build the database once per shape, outside the timed comparison.
    print("priming database ...", flush=True)
    r = run_one("plain", caches[0], args.rows, args.queries, args.payload,
                args.warmup, dbdir, args.kdf)
    print(f"  primed: misses={r.get('misses')} checksum={r.get('checksum')}", flush=True)

    for rep in range(1, args.reps + 1):
        # Rotate arm order every rep so drift cannot masquerade as an arm effect.
        order = [arms[(k + rep - 1) % len(arms)] for k in range(len(arms))]
        for cache in caches:
            for arm in order:
                rec = run_one(arm, cache, args.rows, args.queries, args.payload,
                              args.warmup, dbdir, args.kdf)
                rec.update({"arm": arm, "rep": rep, "cache": cache,
                            "rows": args.rows, "queries": args.queries,
                            "payload": args.payload})
                with open(out, "a") as fh:
                    fh.write(json.dumps(rec) + "\n")
                if rec["ok"]:
                    print(f"  rep{rep:<3} cache={cache:<5} {arm:<8} "
                          f"roi={rec['roi_ns']/1e6:8.3f}ms  dit={rec['dit_on']}  "
                          f"dec/q={rec['decrypts_per_query']:6.3f}  "
                          f"cksum={rec['checksum']}", flush=True)
                else:
                    print(f"  rep{rep:<3} cache={cache:<5} {arm:<8} FAIL {rec['error']}",
                          flush=True)
    print("done ->", out)


if __name__ == "__main__":
    main()
