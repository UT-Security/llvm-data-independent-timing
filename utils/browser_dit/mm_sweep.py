#!/usr/bin/env python3
"""MotionMark filter sweep: what does always-on DIT cost CPU-rasterised filters?

Design notes that matter for reading the output:

* CONFIGURATION IS NOT REPRESENTATIVE. Firefox is forced onto software WebRender
  so the filter runs on the CPU at all. On a normally-composited browser this
  work is a GPU shader and PSTATE.DIT, a CPU state bit, cannot touch it. Any
  number here must carry that label.

* THE WORK IS IN THE GPU PROCESS, named "Nightly GPU Helper" - NOT
  "plugin-container". Measured: during a filter run the GPU process sits at
  ~100% CPU while the content process is at 5%. Hot frames are SWGL's
  draw_quad_spans / draw_quad / DrawElementsInstanced, which are C++ and so
  reachable by a clang pass; webrender::device::gl is Rust and only drives them.

* THE CONTROL IS THE POINT. "CSS bouncing circles" and "CSS bouncing filter
  circles" are the same test page with and without `&filter`, so the pair
  isolates the filter itself rather than CSS animation in general.
"""
import argparse, json, os, pathlib, subprocess, sys, time

HERE = pathlib.Path(__file__).resolve().parent

# arm -> (dylib kind, DIT_ONLY_PROG)
ARMS = {
    "base":    (None,  None),
    "null":    ("off", None),
    "dit":     ("on",  None),                    # every process
    "dit-gpu": ("on",  "Nightly GPU Helper"),    # compositing/rasterisation only
}
TESTS = {
    "filter": "CSSbouncingfiltercircles",
    "plain":  "^CSSbouncingcircles$",            # same page without &filter
}


def canary(work, out, tag):
    j = subprocess.run([str(work / "canary")], capture_output=True, text=True).stdout
    try:
        d = json.loads(j)
    except Exception:
        return None
    d["tag"], d["t"] = tag, time.time()
    with open(out, "a") as fh:
        fh.write(json.dumps(d) + "\n")
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--work", default=str(pathlib.Path.home() / "Documents/dit-browser-bench"))
    ap.add_argument("--reps", type=int, default=15)
    ap.add_argument("--test-interval", type=int, default=15)
    ap.add_argument("--tests", default="filter,plain")
    ap.add_argument("--arms", default="base,null,dit,dit-gpu")
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--start-rep", type=int, default=1,
                    help="resume a partial sweep; suppresses archiving so the "
                         "completed reps are kept. Arm rotation is keyed on the "
                         "rep number, so resuming preserves the original order.")
    args = ap.parse_args()

    work = pathlib.Path(args.work)
    ff = pathlib.Path.home() / ("Documents/firefox/obj-aarch64-apple-darwin25.3.0/"
                                "dist/Nightly.app/Contents/MacOS/firefox")
    mm = work / "MotionMark/MotionMark"
    arms = args.arms.split(",")
    tests = args.tests.split(",")
    results = work / "results-mm-filter.jsonl"
    canary_out = work / "canary-mm-filter.jsonl"
    logs = work / "logs/mm-filter"
    logs.mkdir(parents=True, exist_ok=True)

    if args.start_rep == 1:
        stamp = time.strftime("%Y%m%d-%H%M%S")
        for f in (results, canary_out):
            if f.exists() and f.stat().st_size:
                f.rename(f.with_suffix(f.suffix + "." + stamp + ".bak"))
                print(f"archived previous {f.name}")
    else:
        print(f"resuming at rep {args.start_rep}; keeping existing rows")

    print(f"MotionMark filter sweep: {args.reps} reps x {arms} x {tests}")
    for rep in range(args.start_rep, args.reps + 1):
        print(f"\n--- rep {rep}/{args.reps} ---")
        # Rotate arm order every rep so drift cannot masquerade as an arm effect.
        order = [arms[(k + rep - 1) % len(arms)] for k in range(len(arms))]
        for test in tests:
            for arm in order:
                kind, prog = ARMS[arm]
                c = canary(work, canary_out, f"pre-{test}-{arm}-rep{rep}")
                if c and c.get("dit_lands_on_perm", 1.0) < 0.8:
                    print(f"    !! canary gate: dit_lands_on_perm="
                          f"{c['dit_lands_on_perm']:.3f} before {test}/{arm} rep{rep}")
                cmd = [sys.executable, str(HERE / "run_one_mm.py"),
                       "--arm", f"{test}-{arm}", "--rep", str(rep),
                       "--mm-dir", str(mm), "--browser-bin", str(ff),
                       "--browser-kind", "firefox",
                       "--dylib", "" if kind is None else str(work / f"dit_{kind}.dylib"),
                       "--suite-name", "HTMLsuite", "--test-name", TESTS[test],
                       "--test-interval", str(args.test_interval),
                       "--timeout", "300", "--port", str(args.port),
                       "--software-render", "--out", str(results),
                       "--log-dir", str(logs)]
                # Always build the env explicitly and clear DIT_ONLY_PROG when
                # the arm does not want one: inheriting a stale filter would
                # silently turn an all-process arm into a single-process arm.
                env = dict(os.environ)
                env.pop("DIT_ONLY_PROG", None)
                env.pop("DIT_ONLY_THREAD", None)
                if prog:
                    env["DIT_ONLY_PROG"] = prog
                subprocess.run(cmd, env=env)
                time.sleep(4)
    canary(work, canary_out, "final")
    print("\ndone ->", results)


if __name__ == "__main__":
    main()
