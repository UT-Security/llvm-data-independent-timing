#!/usr/bin/env python3
"""Fixed-work filter sweep: what does always-on DIT cost real browser filter code?

Filters are chosen to line up with the primitives the project already measured as
scalar C ports, so the two instruments can be compared directly:
  none            no filter at all - the control that says how much of any effect
                  belongs to the filter rather than to drawImage/readback
  grayscale(50%)  feColorMatrix family      (ported port measured 1.006x)
  contrast(50%)   feComponentTransfer family(ported port measured 1.007x)
  blur(8px)       feGaussianBlur - separable convolution, the feConvolveMatrix
                  shape (Firefox gem5 port measured 0.997x)

Both source patterns are run because value-predictability is the whole mechanism
under test: `smooth` is a gradient whose pixels are highly predictable, `noise` is
incompressible. If DIT-gated value prediction is worth anything here, the two must
differ - and if they do not, that is itself the finding.
"""
import argparse, json, os, pathlib, subprocess, sys, time

HERE = pathlib.Path(__file__).resolve().parent
ARMS = {"base": None, "null": "off", "dit": "on"}


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
    ap.add_argument("--reps", type=int, default=10)
    ap.add_argument("--start-rep", type=int, default=1)
    ap.add_argument("--filters", default="none,grayscale(50%),contrast(50%),blur(8px)")
    ap.add_argument("--patterns", default="noise,smooth")
    ap.add_argument("--arms", default="base,null,dit")
    ap.add_argument("--iters", type=int, default=150)
    ap.add_argument("--size", type=int, default=512)
    ap.add_argument("--port", type=int, default=8099)
    args = ap.parse_args()

    work = pathlib.Path(args.work)
    ff = pathlib.Path.home() / ("Documents/firefox/obj-aarch64-apple-darwin25.3.0/"
                                "dist/Nightly.app/Contents/MacOS/firefox")
    page = work / "filterbench"
    results = work / "results-fb.jsonl"
    canary_out = work / "canary-fb.jsonl"
    logs = work / "logs/fb-sweep"
    logs.mkdir(parents=True, exist_ok=True)

    filters = args.filters.split(",")
    patterns = args.patterns.split(",")
    arms = args.arms.split(",")

    if args.start_rep == 1:
        stamp = time.strftime("%Y%m%d-%H%M%S")
        for f in (results, canary_out):
            if f.exists() and f.stat().st_size:
                f.rename(f.with_suffix(f.suffix + "." + stamp + ".bak"))
                print(f"archived previous {f.name}")

    total = len(filters) * len(patterns) * len(arms) * (args.reps - args.start_rep + 1)
    print(f"fixed-work filter sweep: {total} runs "
          f"({len(filters)} filters x {len(patterns)} patterns x {len(arms)} arms "
          f"x {args.reps - args.start_rep + 1} reps)")

    for rep in range(args.start_rep, args.reps + 1):
        print(f"\n--- rep {rep}/{args.reps} ---", flush=True)
        order = [arms[(k + rep - 1) % len(arms)] for k in range(len(arms))]
        for pattern in patterns:
            for filt in filters:
                for arm in order:
                    kind = ARMS[arm]
                    c = canary(work, canary_out, f"pre-{pattern}-{filt}-{arm}-rep{rep}")
                    if c and c.get("dit_lands_on_perm", 1.0) < 0.8:
                        print(f"    !! canary gate: dit_lands_on_perm="
                              f"{c['dit_lands_on_perm']:.3f}", flush=True)
                    tag = f"{pattern}-{filt}-{arm}"
                    env = dict(os.environ)
                    env.pop("DIT_ONLY_PROG", None)
                    env.pop("DIT_ONLY_THREAD", None)
                    subprocess.run(
                        [sys.executable, str(HERE / "run_one_fb.py"),
                         "--arm", tag, "--rep", str(rep),
                         "--page-dir", str(page), "--browser-bin", str(ff),
                         "--dylib", "" if kind is None else str(work / f"dit_{kind}.dylib"),
                         "--filter", filt, "--pattern", pattern,
                         "--iters", str(args.iters), "--size", str(args.size),
                         "--reps-in-page", "9", "--software-render",
                         "--port", str(args.port), "--out", str(results),
                         "--log-dir", str(logs)], env=env)
                    time.sleep(2)
    canary(work, canary_out, "final")
    print("\ndone ->", results)


if __name__ == "__main__":
    main()
