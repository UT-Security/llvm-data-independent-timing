#!/usr/bin/env python3
"""One fixed-work filter-bench run.

Replaces the MotionMark approach, whose adaptive controller put the noise floor
(CoV 1.3-2.9%, harness floor +1.95%) ABOVE the DIT effect being measured. This
page does fixed work and reports wall time, so the floor is the machine's.

Still forces software rasterisation: with gfx.canvas.accelerated=false the
canvas filter runs Skia's CPU implementation. Not a deployed configuration, and
every number from it carries that label.
"""
import argparse, json, os, pathlib, shutil, signal, subprocess, sys, tempfile, threading, time
import urllib.parse

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from run_one import (  # noqa: E402
    CLEANUP_PAT, CONTENT_PROC, Handler, QuietHTTPServer, USER_JS, parse_dit_log,
)
from run_one_mm import SOFTWARE_JS  # noqa: E402

# Firefox clamps performance.now() to 1 ms by default (privacy.reduceTimerPrecision),
# which showed up as every median landing on a whole millisecond - ~3% resolution
# on the fastest filter, coarser than the effect being measured. Disabling the
# clamp is required for the timing to mean anything, and is applied to EVERY arm.
BENCH_JS = """
user_pref("privacy.reduceTimerPrecision", false);
user_pref("privacy.resistFingerprinting", false);
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", required=True)
    ap.add_argument("--rep", type=int, required=True)
    ap.add_argument("--page-dir", required=True)
    ap.add_argument("--browser-bin", required=True)
    ap.add_argument("--browser-kind", default="firefox", choices=sorted(CONTENT_PROC))
    ap.add_argument("--dylib", default="")
    ap.add_argument("--filter", default="none")
    ap.add_argument("--pattern", default="noise", choices=["noise", "smooth"])
    ap.add_argument("--iters", type=int, default=150)
    ap.add_argument("--reps-in-page", type=int, default=9)
    ap.add_argument("--size", type=int, default=512)
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--out", required=True)
    ap.add_argument("--log-dir", required=True)
    ap.add_argument("--software-render", action="store_true")
    ap.add_argument("--verbose-dit", action="store_true")
    args = ap.parse_args()

    os.chdir(args.page_dir)
    httpd = QuietHTTPServer(("127.0.0.1", args.port), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()

    profile = tempfile.mkdtemp(prefix=f"fbprof-{args.arm}-")
    if args.browser_kind == "firefox":
        pathlib.Path(profile, "user.js").write_text(
            USER_JS + BENCH_JS + (SOFTWARE_JS if args.software_render else ""))

    env = dict(os.environ)
    env.pop("DYLD_INSERT_LIBRARIES", None)
    env.pop("DIT_VERBOSE", None)
    if args.dylib:
        env["DYLD_INSERT_LIBRARIES"] = args.dylib
    if args.verbose_dit:
        env["DIT_VERBOSE"] = "1"

    # A CSS filter string carries '%' and '(' - a bare '%' is an invalid
    # percent-escape and silently corrupts the parameter, so encode it.
    url = (f"http://127.0.0.1:{args.port}/?filter={urllib.parse.quote(args.filter)}"
           f"&pattern={args.pattern}&iters={args.iters}"
           f"&reps={args.reps_in_page}&size={args.size}")
    logp = pathlib.Path(args.log_dir, f"{args.arm}-rep{args.rep:03d}.log")
    argv = [args.browser_bin, "-profile", profile, "-no-remote", "-new-instance", url]

    record = {"arm": args.arm, "rep": args.rep, "filter": args.filter,
              "pattern": args.pattern, "iters": args.iters, "size": args.size,
              "dylib": args.dylib, "software_render": args.software_render,
              "t_start": time.time()}

    with open(logp, "wb") as log:
        proc = subprocess.Popen(argv, env=env, stdout=log,
                                stderr=subprocess.STDOUT, start_new_session=True)
        ok = Handler.done.wait(args.timeout)
        record["timed_out"] = not ok
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        except Exception:
            proc.terminate()
        try:
            proc.wait(30)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            except Exception:
                proc.kill()

    httpd.shutdown()
    shutil.rmtree(profile, ignore_errors=True)
    for pat in CLEANUP_PAT[args.browser_kind]:
        subprocess.run(["pkill", "-9", "-f", pat], capture_output=True)

    p = Handler.result or {"ok": False, "error": "no result before timeout"}
    record.update({
        "t_end": time.time(),
        "ok": bool(p.get("ok")) and not record["timed_out"],
        "median_ms": p.get("medianMs"), "min_ms": p.get("minMs"),
        "ms_per_iter": p.get("msPerIter"), "all_ms": p.get("allMs"),
        "checksum": p.get("checksum"), "linearity": p.get("linearity"),
        "error": p.get("error"),
        "dit": parse_dit_log(logp.read_text(errors="replace"), args.browser_kind,
                             CONTENT_PROC[args.browser_kind]),
    })
    record["wall_s"] = round(record["t_end"] - record["t_start"], 1)

    with open(args.out, "a") as fh:
        fh.write(json.dumps(record) + "\n")

    if record["ok"]:
        print(f"  {args.arm:>12} rep{args.rep:<3} "
              f"median={record['median_ms']:8.2f}ms  "
              f"ms/iter={record['ms_per_iter']:.4f}  "
              f"lin={record['linearity']:.2f}  "
              f"cksum={record['checksum']}  {record['wall_s']}s")
    else:
        print(f"  {args.arm:>12} rep{args.rep:<3} FAIL  {record.get('error')}  "
              f"{record['wall_s']}s")

    return 0 if record["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
