#!/usr/bin/env python3
"""One MotionMark run, for the CPU-rasterised filter experiment.

Sibling of run_one.py rather than a flag on it: run_one.py is executing inside a
live sweep, and editing a script mid-sweep risks a run reading a half-written
file. Everything shared is imported, not copied.

Why the debug runner: MotionMark's shipping suite has no filter test. The filter
tests live in developer.html's "HTML suite", and the pair that matters is
  CSS bouncing circles         bouncing-css-shapes.html?...&shape=circle
  CSS bouncing filter circles  bouncing-css-shapes.html?...&shape=circle&filter
- identical code path, one differs only by applying a CSS filter, so the two are
a controlled A/B for "what does the filter itself cost under DIT".

Why software rasterisation: on a GPU-composited browser the filter runs as a
shader and PSTATE.DIT, a CPU state bit, cannot touch it. Forcing software
WebRender puts the work back on the CPU. This is NOT a configuration anyone
deploys and any number from it has to carry that label.
"""
import argparse, json, os, pathlib, shutil, signal, subprocess, sys, tempfile, threading, time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from run_one import (  # noqa: E402
    CLEANUP_PAT, CONTENT_PROC, Handler, QuietHTTPServer, USER_JS, parse_dit_log,
)

# Force rasterisation and compositing onto the CPU. Software WebRender (SWGL)
# is the only software path modern Firefox still has - the old Layers+Skia
# fallback is gone - so this is what "filters on the CPU" means here.
SOFTWARE_JS = """
user_pref("gfx.webrender.software", true);
user_pref("gfx.webrender.force-disabled", false);
user_pref("layers.acceleration.disabled", true);
user_pref("gfx.canvas.accelerated", false);
user_pref("media.hardware-video-decoding.enabled", false);
"""

# startBenchmarkImmediatelyIfEncoded REPLACES the option set from the query
# string instead of merging it, so every parameter benchmarkDefaultParameters
# would have supplied has to be present here or the run starts misconfigured.
MM_DEFAULTS = {
    "display": "minimal",
    "tiles": "big",
    "controller": "ramp",
    "kalman-process-error": 1,
    "kalman-measurement-error": 4,
    "time-measurement": "performance",
    "warmup-length": 2000,
    "warmup-frame-count": 30,
    "first-frame-minimum-length": 0,
    "system-frame-rate": 60,
    "frame-rate": 60,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", required=True)
    ap.add_argument("--rep", type=int, required=True)
    ap.add_argument("--mm-dir", required=True,
                    help="directory holding developer.html (…/MotionMark/MotionMark)")
    ap.add_argument("--browser-bin", required=True)
    ap.add_argument("--browser-kind", default="firefox", choices=sorted(CONTENT_PROC))
    ap.add_argument("--dylib", default="")
    ap.add_argument("--suite-name", default="HTMLsuite")
    ap.add_argument("--test-name", required=True,
                    help="case-insensitive REGEX over the test name with \\W stripped, "
                         "e.g. 'CSSbouncingfiltercircles' or '^CSSbouncingcircles$'")
    ap.add_argument("--test-interval", type=int, default=15)
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--out", required=True)
    ap.add_argument("--log-dir", required=True)
    ap.add_argument("--software-render", action="store_true")
    ap.add_argument("--disable-content-sandbox", action="store_true")
    ap.add_argument("--verbose-dit", action="store_true")
    args = ap.parse_args()

    os.chdir(args.mm_dir)
    httpd = QuietHTTPServer(("127.0.0.1", args.port), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()

    profile = tempfile.mkdtemp(prefix=f"mmprof-{args.arm}-")
    if args.browser_kind == "firefox":
        prefs = USER_JS + (SOFTWARE_JS if args.software_render else "")
        pathlib.Path(profile, "user.js").write_text(prefs)

    env = dict(os.environ)
    env.pop("DYLD_INSERT_LIBRARIES", None)
    env.pop("DIT_VERBOSE", None)
    if args.dylib:
        env["DYLD_INSERT_LIBRARIES"] = args.dylib
    if args.verbose_dit:
        env["DIT_VERBOSE"] = "1"
    if args.disable_content_sandbox:
        env["MOZ_DISABLE_CONTENT_SANDBOX"] = "1"

    params = dict(MM_DEFAULTS)
    params["test-interval"] = args.test_interval
    params["suite-name"] = args.suite_name
    params["test-name"] = args.test_name
    # convertQueryStringToObject splits on '=' and does NOT urldecode, so the
    # values must already be URL-safe; suitesFromQueryString decodeURIComponent's
    # the two name fields itself.
    qs = "&".join(f"{k}={v}" for k, v in params.items())
    url = f"http://127.0.0.1:{args.port}/developer.html?{qs}"

    logp = pathlib.Path(args.log_dir, f"{args.arm}-rep{args.rep:03d}.log")
    argv = [args.browser_bin, "-profile", profile, "-no-remote", "-new-instance", url]

    record = {
        "arm": args.arm, "rep": args.rep, "browser_kind": args.browser_kind,
        "suite_name": args.suite_name, "test_name": args.test_name,
        "test_interval": args.test_interval, "software_render": args.software_render,
        "dylib": args.dylib, "started": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "t_start": time.time(),
    }

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
        record["firefox_rc"] = proc.returncode

    httpd.shutdown()
    shutil.rmtree(profile, ignore_errors=True)
    for pat in CLEANUP_PAT[args.browser_kind]:
        subprocess.run(["pkill", "-9", "-f", pat], capture_output=True)

    payload = Handler.result or {"ok": False, "error": "no result before timeout"}
    record.update({
        "t_end": time.time(),
        "ok": bool(payload.get("ok")) and not record["timed_out"],
        "score": payload.get("score"),
        "score_lo": payload.get("scoreLowerBound"),
        "score_hi": payload.get("scoreUpperBound"),
        "ran_tests": payload.get("testNames"),
        "error": payload.get("error"),
        "diag": payload.get("diag"),
        "dit": parse_dit_log(logp.read_text(errors="replace"), args.browser_kind,
                             CONTENT_PROC[args.browser_kind]),
        "log": str(logp),
    })
    record["wall_s"] = round(record["t_end"] - record["t_start"], 1)

    with open(args.out, "a") as fh:
        fh.write(json.dumps(record) + "\n")

    print(f"  {args.arm:>10} rep{args.rep:<3} "
          f"score={record['score'] if record['score'] else 'FAIL':<8} "
          f"procs={record['dit']['processes']} {record['wall_s']}s"
          + ("" if record["ok"] else f"  ERROR: {record.get('error')}"))
    return 0 if record["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
