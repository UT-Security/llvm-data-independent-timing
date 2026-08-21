#!/usr/bin/env python3
"""Measure f_secret for the SQLCipher sweep: the no-encryption calibration.

THE PROBLEM IT SOLVES. `decrypts_per_query` is a clean directly-counted x-axis,
but it is not a fraction. Turning it into one by comparing a cached run to an
uncached run is WRONG: the difference includes the pager read path, which is
public code, and would have put f_secret at 90.8% when the true value is far
lower.

THE METHOD. Run the SAME binary over the SAME logical data with and without
SQLCipher's codec (NOKEY=1 skips PRAGMA key, so the file is plain SQLite). At a
given cache size both runs miss the pager cache the same number of times -
verified, not assumed: 2405 vs 2405 at cache=16, with identical checksums - so
the only difference is AES-256-CBC + HMAC per page.

    f_secret(cache) = (ROI_encrypted - ROI_plain) / ROI_encrypted

Arms alternate and rotate per rep so drift cannot masquerade as a mode effect.
"""
import argparse, json, os, pathlib, re, statistics as st, subprocess, time

ROI_RE = re.compile(r"ROI_NS (\d+) DIT (\d)")
SUM_RE = re.compile(r"misses=(\d+) decrypts_per_query=([\d.]+) encrypted=(\d) checksum=(\d+)")


def run(exe, cache, nokey, qdb, rows, queries, payload, warmup, kdf):
    env = dict(os.environ)
    env.update({"CACHE_SIZE": str(cache), "KDF_ITER": str(kdf), "QDB": str(qdb)})
    if nokey:
        env["NOKEY"] = "1"
    else:
        env.pop("NOKEY", None)
    out = subprocess.run([str(exe), "--rows", str(rows), "--queries", str(queries),
                          "--payload", str(payload), "--warmup", str(warmup), "--reuse"],
                         capture_output=True, text=True, env=env, timeout=600).stdout
    m1, m2 = ROI_RE.search(out), SUM_RE.search(out)
    if not m1 or not m2:
        return None
    return {"roi_ns": int(m1.group(1)), "misses": int(m2.group(1)),
            "dec_q": float(m2.group(2)), "encrypted": int(m2.group(3)),
            "checksum": int(m2.group(4))}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=str(pathlib.Path.home() /
                                         "Documents/dit-browser-bench/calbin/q_native"))
    ap.add_argument("--dbdir", default=str(pathlib.Path.home() /
                                           "Documents/dit-browser-bench/sqlcdb"))
    ap.add_argument("--caches", default="16,64,256,1024,1408,1664,1792,1920,2048,4096")
    ap.add_argument("--reps", type=int, default=10)
    ap.add_argument("--rows", type=int, default=2000)
    ap.add_argument("--queries", type=int, default=2000)
    ap.add_argument("--payload", type=int, default=3500)
    ap.add_argument("--warmup", type=int, default=200)
    ap.add_argument("--kdf", type=int, default=4000)
    ap.add_argument("--out", default=str(pathlib.Path.home() /
                                         "Documents/dit-browser-bench/results-sqlc-cal.jsonl"))
    args = ap.parse_args()

    d = pathlib.Path(args.dbdir)
    enc_db = d / f"q_r{args.rows}_p{args.payload}.db"
    plain_db = d / f"plain_r{args.rows}_p{args.payload}.db"
    out = pathlib.Path(args.out)
    if out.exists() and out.stat().st_size:
        out.rename(out.with_suffix(out.suffix + "." + time.strftime("%Y%m%d-%H%M%S") + ".bak"))

    caches = [int(c) for c in args.caches.split(",")]
    print(f"calibration: {len(caches)} caches x 2 modes x {args.reps} reps")
    rows_out = []
    for rep in range(1, args.reps + 1):
        for cache in caches:
            modes = [(False, enc_db), (True, plain_db)]
            if rep % 2 == 0:
                modes.reverse()          # rotate so order cannot bias a mode
            for nokey, db in modes:
                r = run(args.exe, cache, nokey, db, args.rows, args.queries,
                        args.payload, args.warmup, args.kdf)
                if r is None:
                    print(f"  rep{rep} cache={cache} nokey={nokey} FAILED")
                    continue
                r.update({"rep": rep, "cache": cache})
                rows_out.append(r)
                with open(out, "a") as fh:
                    fh.write(json.dumps(r) + "\n")
        print(f"  rep {rep}/{args.reps} done", flush=True)

    print(f"\n{'cache':>6} {'misses(enc)':>12} {'misses(plain)':>14} "
          f"{'ROI enc ms':>11} {'ROI plain ms':>13} {'f_secret':>10}")
    print("-" * 72)
    for cache in caches:
        e = [r for r in rows_out if r["cache"] == cache and r["encrypted"] == 1]
        p = [r for r in rows_out if r["cache"] == cache and r["encrypted"] == 0]
        if not e or not p:
            continue
        me, mp = st.median([r["roi_ns"] for r in e]), st.median([r["roi_ns"] for r in p])
        mis_e = st.median([r["misses"] for r in e])
        mis_p = st.median([r["misses"] for r in p])
        f = (me - mp) / me * 100 if me else float("nan")
        flag = "" if mis_e == mis_p else "  << MISS COUNTS DIFFER, subtraction not clean"
        print(f"{cache:>6} {mis_e:>12.0f} {mis_p:>14.0f} {me/1e6:>11.2f} "
              f"{mp/1e6:>13.2f} {f:>9.1f}%{flag}")


if __name__ == "__main__":
    main()
