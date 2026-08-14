#!/usr/bin/env python3
"""Run Speedometer 3.1 once in Firefox under one arm and append a JSON result.

One process = one measurement. The sweep script calls this repeatedly so that a
single crashed or hung run costs one data point instead of the whole night.

Serves the local Speedometer copy itself and waits for the page to POST its own
score back (see inject.js), so no geckodriver/selenium is required.
"""

import argparse
import json
import os
import pathlib
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

# Which process actually runs the benchmark, per engine. The harness verifies DIT
# reached THIS process; covering only the UI process would measure nothing.
#   Firefox  -> content processes are `plugin-container`, fork+exec'd, so they
#               inherit DYLD_INSERT_LIBRARIES from the parent.
#   WebKit   -> `com.apple.WebKit.WebContent`. Launched as an XPC service, which
#               does NOT inherit the parent environment by default. Whether
#               injection reaches it is an empirical question the verify phase
#               answers; if it does not, patch WTF::Thread::entryPoint in the
#               WebKit source instead (we build it from source anyway).
CONTENT_PROC = {
    "firefox": ("plugin-container",),
    "minibrowser": ("WebContent", "com.apple.WebKit.WebContent"),
    # Chromium's renderer executable is literally "Chromium Helper (Renderer)".
    # Match only the renderer, not the GPU/Alerts/utility helpers, since the
    # renderer is the one that runs Speedometer.
    "chromium": ("Chromium Helper (Renderer)",),
}

# What to pkill between runs. Broader than CONTENT_PROC on purpose: any leftover
# helper of any type would contend with the next arm.
CLEANUP_PAT = {
    "firefox": ("plugin-container",),
    "minibrowser": ("WebContent",),
    "chromium": ("Chromium Helper",),
}

# Applied on top of USER_JS when --single-thread is set.
SINGLE_THREAD_JS = """
user_pref("dom.ipc.processCount", 1);
user_pref("dom.ipc.processCount.webIsolated", 1);
user_pref("javascript.options.parallel_parsing", false);
user_pref("javascript.options.ion.offthread_compilation", false);
"""

# Firefox profile prefs. Keep it deterministic and quiet: no first-run tour, no
# telemetry upload, no update check, no session restore. Any of these would land
# in the middle of a measurement window.
USER_JS = """
user_pref("app.update.enabled", false);
user_pref("app.update.auto", false);
user_pref("browser.aboutwelcome.enabled", false);
user_pref("browser.newtabpage.enabled", false);
user_pref("browser.sessionstore.resume_from_crash", false);
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("datareporting.healthreport.uploadEnabled", false);
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("toolkit.telemetry.enabled", false);
user_pref("toolkit.telemetry.unified", false);
"""


class QuietHTTPServer(ThreadingHTTPServer):
    """Swallow the severed-socket errors that killing the browser causes.

    The harness kills the browser the instant it has the score, which cuts off
    whatever resource requests were still in flight. Each severed socket surfaces
    as BrokenPipeError/ConnectionResetError in a handler thread, and
    socketserver's default handle_error prints a full traceback for every one of
    them. It is pure noise - it happens after the score is captured, during
    teardown, identically in every arm - but it buries real errors and looks like
    a failure. Anything that is not a severed socket still gets reported.
    """

    daemon_threads = True

    def handle_error(self, request, client_address):
        if isinstance(sys.exc_info()[1], (BrokenPipeError, ConnectionResetError)):
            return
        super().handle_error(request, client_address)


class Handler(SimpleHTTPRequestHandler):
    result = None
    done = threading.Event()

    def log_message(self, *args):  # keep stdout clean; harness owns the log
        pass

    def log_error(self, *args):  # ditto: severed sockets are expected here
        pass

    def _accept(self, payload):
        if Handler.result is None:
            Handler.result = payload
            Handler.done.set()

    def do_POST(self):
        if self.path.split("?")[0] != "/result":
            self.send_error(404)
            return
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n).decode("utf-8", "replace")
        try:
            payload = json.loads(raw)
        except Exception as exc:
            payload = {"ok": False, "error": f"bad json: {exc}", "raw": raw[:2000]}
        self._accept(payload)
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        if self.path.split("?")[0] == "/result":
            self._accept({"ok": False, "error": "navigated to /result: " + self.path})
            self.send_response(204)
            self.end_headers()
            return
        super().do_GET()


def parse_dit_log(text, kind="firefox", content_pats=None):
    """Summarise the [dit] lines the injected dylib writes to stderr.

    threads_total comes from task_threads() and counts EVERY thread in the
    process, including libdispatch workqueue threads. Those are not created via
    pthread_create, so they do not get DIT (verified: a dispatch_async worker
    reads DIT=0 under injection). Keeping both numbers visible stops that gap
    from quietly deflating the measured overhead.

    Processes that appear only in an exit/thread line and never in a load line
    are fork() children: they never re-ran the constructor, but fork preserves
    the calling thread's PSTATE, so they inherit DIT and are not a coverage
    hole. They are excluded from the main_dit tally rather than counted as
    failures.
    """
    # NB: prog can contain spaces - Chromium's renderer is literally
    # "Chromium Helper (Renderer)". The patterns below bound the name with a
    # non-greedy capture rather than \S+, which silently dropped 10 of 13
    # processes on the first Chromium run.
    procs = {}

    def rec(pid, prog):
        p = procs.setdefault(pid, {})
        p["prog"] = prog
        return p

    for pid, prog, enable, main_dit, applies in re.findall(
        r"\[dit\] load pid=(\d+) prog=(.+?) enable=(\d+) main_dit=(\d+)"
        r"(?: applies=(\d+))?", text
    ):
        p = rec(pid, prog)
        p["enable"] = int(enable)
        p["main_dit"] = int(main_dit)
        p["applies"] = int(applies) if applies else 1

    # Emitted per thread when DIT_VERBOSE is set; the highest n/total seen is the
    # best available snapshot, since SIGKILL means the destructor may never run.
    for pid, prog, n, total, dit in re.findall(
        r"\[dit\] thread pid=(\d+) prog=(.+?) n=(\d+) total=(\d+) dit=(\d+)", text
    ):
        p = rec(pid, prog)
        p["threads_started"] = max(int(n), p.get("threads_started", 0))
        p["threads_total"] = max(int(total), p.get("threads_total", 0))
        if int(dit) != 1:
            p["thread_dit_miss"] = p.get("thread_dit_miss", 0) + 1

    for pid, prog, started, total in re.findall(
        r"\[dit\] exit pid=(\d+) prog=(.+?) threads_started=(\d+) threads_total=(\d+)",
        text,
    ):
        p = rec(pid, prog)
        p["threads_started"] = max(int(started), p.get("threads_started", 0))
        p["threads_total"] = max(int(total), p.get("threads_total", 0))

    pats = content_pats or CONTENT_PROC.get(kind, CONTENT_PROC["firefox"])
    # A process excluded by DIT_ONLY_PROG has applies=0: it is meant to run
    # without DIT, so it must not count as a coverage failure. Only processes
    # DIT was supposed to apply to are checked.
    loaded = [p for p in procs.values() if "main_dit" in p and p.get("applies", 1)]
    optedout = [p for p in procs.values() if "main_dit" in p and not p.get("applies", 1)]
    content = [p for p in procs.values()
               if any(pat in (p.get("prog") or "") for pat in pats)]
    return {
        "processes": len(procs),
        "processes_loaded": len(loaded),
        "processes_opted_out": len(optedout),
        "content_processes": len(content),
        "all_main_dit_set": bool(loaded) and all(p["main_dit"] == 1 for p in loaded),
        "content_dit_set": bool(content)
        and all(p.get("main_dit") == 1 for p in content if "main_dit" in p),
        "detail": procs,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", required=True)
    ap.add_argument("--rep", type=int, required=True)
    ap.add_argument("--speedometer-dir", required=True)
    ap.add_argument("--browser-bin", required=True,
                    help="firefox binary, or MiniBrowser binary / run-minibrowser script")
    ap.add_argument("--browser-kind", default="firefox",
                    choices=sorted(CONTENT_PROC), help="how to launch and what the "
                    "content process is called")
    ap.add_argument("--dylib", default="", help="empty = no injection (baseline arm)")
    ap.add_argument("--iterations", type=int, default=10)
    ap.add_argument("--timeout", type=int, default=900)
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--out", required=True, help="results .jsonl to append to")
    ap.add_argument("--log-dir", required=True)
    ap.add_argument("--disable-content-sandbox", action="store_true")
    ap.add_argument("--verbose-dit", action="store_true",
                    help="log every wrapped thread; for verify, not timed runs")
    ap.add_argument("--single-thread", action="store_true",
                    help="minimise engine parallelism: one renderer, no background "
                         "JIT/GC/style threads. Speedometer's critical path is the "
                         "renderer main thread either way; this strips the rest.")
    ap.add_argument("--ecore-exec", default="",
                    help="path to the ecore_exec helper; required with --ecore")
    ap.add_argument("--ecore", action="store_true",
                    help="run under taskpolicy -b (background QoS), which steers "
                         "threads onto E-cores. Causal control: the LVP is ~absent "
                         "there (1.16x vs 4.01x), so if the DIT cost is the LVP it "
                         "should largely vanish.")
    args = ap.parse_args()

    os.chdir(args.speedometer_dir)
    httpd = QuietHTTPServer(("127.0.0.1", args.port), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()

    profile = tempfile.mkdtemp(prefix=f"prof-{args.arm}-")
    if args.browser_kind == "firefox":
        prefs = USER_JS + (SINGLE_THREAD_JS if args.single_thread else "")
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
    if args.single_thread and args.browser_kind == "firefox":
        env["STYLO_THREADS"] = "1"   # Gecko's parallel style system

    url = (
        f"http://127.0.0.1:{args.port}/"
        f"?startAutomatically=true&iterationCount={args.iterations}"
    )
    logp = pathlib.Path(args.log_dir, f"{args.arm}-rep{args.rep:03d}.log")

    if args.browser_kind == "firefox":
        argv = [args.browser_bin, "-profile", profile, "-no-remote",
                "-new-instance", url]
    elif args.browser_kind == "chromium":
        # Fresh profile per run, and switch off everything that would otherwise
        # do background work in the middle of a measurement window.
        argv = [args.browser_bin,
                f"--user-data-dir={profile}",
                "--no-first-run", "--no-default-browser-check",
                "--disable-background-networking",
                "--disable-background-timer-throttling",
                "--disable-component-update",
                "--disable-sync",
                "--metrics-recording-only",
                "--no-service-autorun",
                "--password-store=basic",
                url]
        if args.disable_content_sandbox:
            argv.insert(1, "--no-sandbox")
        if args.single_thread:
            # --renderer-process-limit is only a soft cap; Chromium ignored it and
            # still spawned 4 renderers. --single-process genuinely collapses
            # everything into one process (measured: 0 helper renderers, 3 procs
            # total, vs 13). --process-per-site made it worse, not better.
            argv[1:1] = ["--single-process",
                         "--js-flags=--single-threaded",  # no V8 background JIT/GC
                         "--num-raster-threads=1"]
    else:
        # MiniBrowser takes a bare URL. Passing the run-minibrowser wrapper works
        # too: it sets DYLD_FRAMEWORK_PATH for the local build and execs, and the
        # process-group kill below reaps whatever it spawned.
        argv = [args.browser_bin, url]

    # In Chromium's --single-process mode there is no helper renderer: the main
    # "Chromium" process runs the page, so that is what has to carry DIT.
    content_pats = CONTENT_PROC[args.browser_kind]
    if args.single_thread and args.browser_kind == "chromium":
        content_pats = ("Chromium",)

    if args.ecore:
        # NOT `taskpolicy -b`: that is an Apple platform binary, so dyld strips
        # DYLD_INSERT_LIBRARIES across it and the browser runs uninjected
        # (measured: procs=0, timeout). ecore_exec is ours, so the insert
        # survives the exec. See ecore_exec.c.
        if not args.ecore_exec:
            sys.exit("--ecore requires --ecore-exec")
        argv = [args.ecore_exec] + argv

    record = {
        "arm": args.arm,
        "rep": args.rep,
        "browser_kind": args.browser_kind,
        "single_thread": args.single_thread,
        "ecore": args.ecore,
        "iterations": args.iterations,
        "dylib": args.dylib,
        "started": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "t_start": time.time(),
    }

    with open(logp, "wb") as log:
        proc = subprocess.Popen(
            argv,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
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
    # Anything the browser left behind would contend with the next arm.
    for pat in CLEANUP_PAT[args.browser_kind]:
        subprocess.run(["pkill", "-9", "-f", pat], capture_output=True)

    payload = Handler.result or {"ok": False, "error": "no result before timeout"}
    record.update(
        {
            "t_end": time.time(),
            "ok": bool(payload.get("ok")) and not record["timed_out"],
            "score": payload.get("score"),
            "score_delta": payload.get("delta"),
            "score_values": payload.get("values"),
            "displayed": payload.get("displayed"),
            "error": payload.get("error"),
            "dit": parse_dit_log(logp.read_text(errors="replace"), args.browser_kind,
                                 content_pats),
            "log": str(logp),
        }
    )
    record["wall_s"] = round(record["t_end"] - record["t_start"], 1)

    with open(args.out, "a") as fh:
        fh.write(json.dumps(record) + "\n")

    print(
        f"  {args.arm:>8} rep{args.rep:<3} "
        f"score={record['score'] if record['score'] else 'FAIL':<8} "
        f"procs={record['dit']['processes']} "
        f"ditset={record['dit']['all_main_dit_set']} "
        f"{record['wall_s']}s"
        + ("" if record["ok"] else f"  ERROR: {record.get('error')}")
    )
    return 0 if record["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
