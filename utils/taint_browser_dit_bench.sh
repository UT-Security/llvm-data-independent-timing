#!/bin/bash
# taint_browser_dit_bench.sh - does always-on PSTATE.DIT cost anything on a real
# mixed browser workload?
#
# Measures Speedometer 3.1 in a locally built browser under three arms:
#
#   base  no injection at all
#   null  the interposer dylib injected, but compiled with DIT_ENABLE=0
#   dit   the interposer dylib injected, PSTATE.DIT set on every thread
#
# base vs null  = what the harness itself costs (should be ~0)
# null vs dit   = the honest cost of always-on DIT
#
# The reference is FLOP (USENIX Sec'25) section 7: 4.5% on Speedometer 3.0 with
# DIT set process-wide in Safari.
#   BROWSER=firefox     (default) answers the binary question - is always-on DIT
#                       expensive on a real browser workload on this silicon.
#                       MEASURED 2026-08-09: +2.61% +/-0.51, 20/20 reps slower.
#   BROWSER=minibrowser locally built WebKit = Safari's engine = the closest
#                       apples-to-apples comparison with FLOP available to us.
# See docs/research/browser-history-leaks.md.
#
# Building WebKit for the minibrowser arm (hours, do not run under Claude):
#   git clone https://github.com/WebKit/WebKit.git ~/Documents/WebKit
#   cd ~/Documents/WebKit && Tools/Scripts/build-webkit --release
# then: BROWSER=minibrowser REPS=20 utils/taint_browser_dit_bench.sh
#
# RUN THIS WITH NOTHING ELSE ON THE MACHINE. No Claude, no editor, no browser.
# A run at ITERATIONS=2 measured 5.5s wall, so budget roughly 20-40 min for the
# default REPS=10, and prefer REPS=20 - the sweep is cheap and the effect being
# chased (~4.5%) is not much larger than the run-to-run spread.
#
#   utils/taint_browser_dit_bench.sh                 # setup + verify + bench
#   REPS=20 utils/taint_browser_dit_bench.sh         # recommended
#   SINGLE=1 ...   one process, no background JIT/GC/raster threads
#   ECORE=1  ...   run on E-cores, where the LVP is worth ~1.2x not 4.0x
# SINGLE/ECORE tag the output files (-1t, -ecore) so they cannot overwrite the
# multi-threaded P-core baseline.
#   REPS=3 ITERATIONS=5 utils/taint_browser_dit_bench.sh   # quick shakedown
#   utils/taint_browser_dit_bench.sh verify          # just the sanity checks
#   utils/taint_browser_dit_bench.sh report          # re-summarise existing data
#
# Everything lands in $WORK_DIR (default ~/Documents/dit-browser-bench). Results
# are per browser so the two engines can never be pooled by accident:
#   results-$BROWSER.jsonl  one line per run  <- the file to hand back to Claude
#   canary-$BROWSER.jsonl   thermal + DIT-works probe around every run
#   env-$BROWSER.json       machine and build provenance
#   summary-$BROWSER.txt    human-readable aggregate
#   logs/$BROWSER/          per-run browser stderr, incl. [dit] coverage lines

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/browser_dit"

WORK_DIR="${WORK_DIR:-$HOME/Documents/dit-browser-bench}"

# BROWSER=firefox     locally built Firefox Nightly (default)
# BROWSER=minibrowser locally built WebKit -> Safari's engine, the FLOP comparison.
#                     Shipped Safari cannot be used: it carries the
#                     library-validation codesign flag, and its WebContent
#                     processes are launched as XPC services that do not inherit
#                     DYLD_INSERT_LIBRARIES.
BROWSER="${BROWSER:-firefox}"
FIREFOX="${FIREFOX:-$HOME/Documents/firefox/obj-aarch64-apple-darwin25.3.0/dist/Nightly.app/Contents/MacOS/firefox}"
WEBKIT_DIR="${WEBKIT_DIR:-$HOME/Documents/WebKit}"
MINIBROWSER="${MINIBROWSER:-$WEBKIT_DIR/WebKitBuild/Release/MiniBrowser.app/Contents/MacOS/MiniBrowser}"
CHROMIUM="${CHROMIUM:-$WORK_DIR/chromium/chrome-mac/Chromium.app/Contents/MacOS/Chromium}"
case "$BROWSER" in
    firefox)     BROWSER_BIN="$FIREFOX" ;;
    minibrowser) BROWSER_BIN="$MINIBROWSER" ;;
    chromium)    BROWSER_BIN="$CHROMIUM" ;;
    *)           echo "unknown BROWSER=$BROWSER (firefox|chromium|minibrowser)" >&2; exit 1 ;;
esac

REPS="${REPS:-10}"
ITERATIONS="${ITERATIONS:-10}"
PORT="${PORT:-8099}"
TIMEOUT="${TIMEOUT:-900}"
SPEEDOMETER_REF="${SPEEDOMETER_REF:-1386415be8fef2f6b6bbdbe1828872471c5d802a}"  # release/3.1

# SINGLE=1  minimise engine parallelism (one renderer, no background JIT/GC/style
#           threads). Speedometer's critical path is the renderer main thread
#           regardless; this strips the rest so the measurement is one hot thread.
# ECORE=1   run the browser under `taskpolicy -b`, steering it onto E-cores.
#           Causal control: measured on this M5, the LVP is worth 4.01x on a
#           P-core but only 1.16x on an E-core, so if the DIT cost really is the
#           LVP it should mostly vanish here. True single-CORE pinning does not
#           exist on Apple silicon (no taskset; THREAD_AFFINITY_POLICY is a no-op).
# RENDERER_ONLY=1 sets DIT only in the process that runs the page, which is what
# FLOP section 7 actually did: "we patched Safari to set the DIT bit in the
# rendering process ... 4.5% on the Speedometer 3.0 benchmark". Our default arms
# set it everywhere, so they are an upper bound relative to that methodology.
case "$BROWSER" in
    firefox)     RENDER_PROG="plugin-container" ;;
    chromium)    RENDER_PROG="Chromium Helper (Renderer)" ;;
    minibrowser) RENDER_PROG="WebContent" ;;
esac

EXTRA_RUN_ARGS=""
[[ "${SINGLE:-0}" == "1" ]] && EXTRA_RUN_ARGS="$EXTRA_RUN_ARGS --single-thread"
[[ "${ECORE:-0}" == "1" ]] && EXTRA_RUN_ARGS="$EXTRA_RUN_ARGS --ecore --ecore-exec $WORK_DIR/ecore_exec"
[[ "${RENDERER_ONLY:-0}" == "1" ]] && export DIT_ONLY_PROG="$RENDER_PROG"
VARIANT=""
[[ "${RENDERER_ONLY:-0}" == "1" ]] && VARIANT="$VARIANT-rend"
[[ "${SINGLE:-0}" == "1" ]] && VARIANT="$VARIANT-1t"
[[ "${ECORE:-0}" == "1" ]] && VARIANT="$VARIANT-ecore"

# Speedometer clone, dylibs and canary are shared; results are per browser so the
# two engines' data can never be pooled by accident.
SP_DIR="$WORK_DIR/Speedometer"
LOG_DIR="$WORK_DIR/logs/$BROWSER$VARIANT"
RESULTS="$WORK_DIR/results-$BROWSER$VARIANT.jsonl"
CANARY_OUT="$WORK_DIR/canary-$BROWSER$VARIANT.jsonl"
SANDBOX_FLAG_FILE="$WORK_DIR/.sandbox_flag-$BROWSER"

# Do not let the machine sleep or nap mid-sweep.
if [[ "${DIT_BENCH_CAFFEINATED:-}" != "1" ]]; then
    export DIT_BENCH_CAFFEINATED=1
    exec caffeinate -dims "$0" "$@"
fi

say() { printf '\n=== %s ===\n' "$*"; }
die() { printf '\nFATAL: %s\n' "$*" >&2; exit 1; }

# --------------------------------------------------------------------------
setup() {
    say "setup [$BROWSER]"
    mkdir -p "$WORK_DIR" "$LOG_DIR"

    [[ -x "$BROWSER_BIN" ]] || die "no $BROWSER binary at $BROWSER_BIN
  firefox     : override with FIREFOX=
  chromium    : run '$0 fetch-chromium' to download a prebuilt arm64 snapshot
  minibrowser : build WebKit first (see the header of this script), or MINIBROWSER="

    if [[ ! -d "$SP_DIR/.git" ]]; then
        echo "cloning Speedometer 3.1 ..."
        git clone -q https://github.com/WebKit/Speedometer.git "$SP_DIR"
    fi
    git -C "$SP_DIR" fetch -q origin
    git -C "$SP_DIR" checkout -q "$SPEEDOMETER_REF"
    grep -q "dit-inject.js" "$SP_DIR/index.html" || {
        cp "$SRC/inject.js" "$SP_DIR/dit-inject.js"
        # Classic script before </body>; it polls for benchmarkClient, so it does
        # not matter that the deferred module has not run yet.
        python3 - "$SP_DIR/index.html" <<'PY'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text()
assert "</body>" in s, "unexpected index.html"
p.write_text(s.replace("</body>", '    <script src="dit-inject.js"></script>\n    </body>', 1))
PY
        echo "injected result collector into Speedometer index.html"
    }

    echo "compiling dylibs + canary ..."
    xcrun clang -O2 -dynamiclib -DDIT_ENABLE=1 "$SRC/dit_all_threads.c" -o "$WORK_DIR/dit_on.dylib"
    xcrun clang -O2 -dynamiclib -DDIT_ENABLE=0 "$SRC/dit_all_threads.c" -o "$WORK_DIR/dit_off.dylib"
    xcrun clang -O2 "$SRC/canary.c" -o "$WORK_DIR/canary"
    xcrun clang -O2 "$SRC/ecore_exec.c" -o "$WORK_DIR/ecore_exec"
    # arm64e variants, needed only to inject into Apple platform binaries (they
    # reject a plain arm64 dylib with "incompatible architecture"). Useful only
    # if amfi_get_out_of_my_way=1 is set, since Safari and WebContent also carry
    # the library-validation codesign flag. Best-effort: third-party arm64e is
    # not a supported configuration.
    xcrun clang -O2 -arch arm64e -dynamiclib -DDIT_ENABLE=1 "$SRC/dit_all_threads.c" \
        -o "$WORK_DIR/dit_on_arm64e.dylib" 2>/dev/null || true
    xcrun clang -O2 -arch arm64e -dynamiclib -DDIT_ENABLE=0 "$SRC/dit_all_threads.c" \
        -o "$WORK_DIR/dit_off_arm64e.dylib" 2>/dev/null || true

    python3 - "$WORK_DIR/env-$BROWSER.json" "$BROWSER_BIN" "$SP_DIR" "$BROWSER" <<'PY'
import json, subprocess, sys, time, platform
out, ff, sp, kind = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
def sh(*c):
    try: return subprocess.run(c, capture_output=True, text=True).stdout.strip()
    except Exception: return None
json.dump({
    "when": time.strftime("%Y-%m-%dT%H:%M:%S"),
    "machine": sh("sysctl", "-n", "machdep.cpu.brand_string"),
    "ncpu": sh("sysctl", "-n", "hw.ncpu"),
    "memsize": sh("sysctl", "-n", "hw.memsize"),
    "feat_dit": sh("sysctl", "-n", "hw.optional.arm.FEAT_DIT"),
    "os": platform.mac_ver()[0],
    "browser_kind": kind,
    "browser_bin": ff,
    "browser_version": sh(ff, "--version") if kind == "firefox" else None,
    "speedometer_rev": sh("git", "-C", sp, "rev-parse", "HEAD"),
}, open(out, "w"), indent=2)
PY
    echo "wrote $WORK_DIR/env-$BROWSER.json"
}

# --------------------------------------------------------------------------
canary() {  # $1 = tag
    local j
    if [[ "${ECORE:-0}" == "1" ]]; then
        j="$("$WORK_DIR/ecore_exec" "$WORK_DIR/canary")"
    else
        j="$("$WORK_DIR/canary")"
    fi
    python3 -c '
import json,sys,time
d=json.loads(sys.argv[1]); d["tag"]=sys.argv[2]; d["t"]=time.time()
print(json.dumps(d))' "$j" "$1" >> "$CANARY_OUT"
    echo "$j"
}

verify() {
    say "verify [$BROWSER$VARIANT]"
    [[ -x "$WORK_DIR/canary" ]] || die "run setup first"
    mkdir -p "$LOG_DIR"   # variant dirs are not created by setup

    echo "canary (LVP present? DIT effective?)"
    local c; c="$(canary verify)"
    echo "  $c"
    python3 -c '
import json,sys,os
d=json.loads(sys.argv[1]); ecore = os.environ.get("ECORE") == "1"
if d["lvp_ratio"] < 2.0 and not ecore:
    sys.exit("no LVP headroom on this machine (lvp_ratio=%.2f) - DIT has nothing to gate, "
             "and a null browser result would be uninterpretable" % d["lvp_ratio"])
if ecore:
    print("  E-CORE ARM: lvp_ratio=%.2f (vs ~4.0 on P-cores). A low value here is the "
          "POINT of this arm - if the browser DIT cost also collapses, the cost is "
          "the LVP." % d["lvp_ratio"])
if d["dit_effect"] < 2.0 and not ecore:
    sys.exit("PSTATE.DIT did not slow the const chase (dit_effect=%.2f) - DIT is not taking "
             "effect, every browser number would be meaningless" % d["dit_effect"])
lop = d.get("dit_lands_on_perm")
if lop is not None and not (0.8 <= lop <= 1.25):
    sys.exit("with DIT set, the const chase did not land on the perm line (%.3f) - "
             "DIT is not fully disabling the LVP" % lop)
print("  OK: lvp_ratio=%.2f dit_effect=%.2f dit_lands_on_perm=%.3f"
      % (d["lvp_ratio"], d["dit_effect"], lop if lop is not None else float("nan")))' "$c"

    # Does the dylib reach the CONTENT process? Speedometer runs there, so if it
    # does not, the measurement reads ~0% for the wrong reason. Try with the
    # sandbox on first; fall back to disabling it, and record which was used so
    # both arms get the same treatment.
    local smoke="$WORK_DIR/smoke.jsonl" sandbox_flag="" covered=0
    rm -f "$smoke"
    for attempt in sandbox-on sandbox-off; do
        echo "smoke run ($attempt) ..."
        local flag=""
        [[ "$attempt" == "sandbox-off" ]] && flag="--disable-content-sandbox"
        python3 "$SRC/run_one.py" --arm smoke --rep 0 \
            --speedometer-dir "$SP_DIR" --browser-bin "$BROWSER_BIN" \
            --browser-kind "$BROWSER" \
            --dylib "$WORK_DIR/dit_on.dylib" --iterations 1 --timeout 300 \
            --port "$PORT" --out "$smoke" --log-dir "$LOG_DIR" --verbose-dit $flag $EXTRA_RUN_ARGS || true
        if python3 -c '
import json,sys
r=[json.loads(l) for l in open(sys.argv[1])][-1]
d=r["dit"]
sys.exit(0 if (r["ok"] and d["content_processes"] and d["content_dit_set"]
               and d["all_main_dit_set"]) else 1)
' "$smoke"; then
            sandbox_flag="$flag"
            covered=1
            echo "  OK: DIT reached the content process ($attempt)"
            break
        fi
        echo "  content process not covered under $attempt"
    done
    [[ "$covered" == "1" ]] || die "never reached the content process; patch mozglue instead"
    python3 -c '
import json,sys
r=[json.loads(l) for l in open(sys.argv[1])][-1]
d=r["dit"]
print("  processes: %d loaded (%d content)" % (d["processes_loaded"], d["content_processes"]))
covs=[]
for pid,p in sorted(d["detail"].items(), key=lambda kv:int(kv[0])):
    s,t = p.get("threads_started"), p.get("threads_total")
    if t:
        # threads_started is cumulative, threads_total is a live census, so
        # threads that already exited can push the ratio past 1. Clamp.
        frac=min(1.0, ((s or 0)+1)/t); covs.append(frac)
        cov="%d wrapped / %d live (%.0f%%)" % ((s or 0)+1, t, 100*frac)
    else:
        cov="thread census unavailable"
    md = p.get("main_dit")
    print("    pid %-7s %-19s main_dit=%-4s %s" % (pid, p.get("prog"), md if md is not None else "fork", cov))
if covs:
    m=sum(covs)/len(covs)
    print("  mean pthread_create coverage: %.0f%% (remainder = libdispatch workers, uncovered)" % (100*m))
    if m < 0.8:
        print("  NOTE: coverage is partial, so the measured DIT cost is a LOWER BOUND")' "$smoke"

    echo "$sandbox_flag" > "$SANDBOX_FLAG_FILE"
    say "verify PASSED"
}

# --------------------------------------------------------------------------
bench() {
    say "bench [$BROWSER$VARIANT]: $REPS reps x 3 arms x ${ITERATIONS} Speedometer iterations"
    [[ -f "$SANDBOX_FLAG_FILE" ]] || die "run verify first"
    mkdir -p "$LOG_DIR"

    # Never append to a previous sweep. Rep numbers restart at 1, so appending
    # would collide with the old run's reps, and the paired analysis keys on
    # (arm, rep) - it would silently keep whichever row came last and quietly
    # blend two sweeps. Archive instead of deleting: a killed run's partial data
    # is still worth keeping.
    local stamp; stamp="$(date +%Y%m%d-%H%M%S)"
    for f in "$RESULTS" "$CANARY_OUT"; do
        if [[ -s "$f" ]]; then
            mv "$f" "$f.$stamp.bak"
            echo "archived previous $(basename "$f") -> $(basename "$f").$stamp.bak"
        fi
    done
    local sandbox_flag; sandbox_flag="$(cat "$SANDBOX_FLAG_FILE")"
    local arms=(base null dit)

    for ((rep = 1; rep <= REPS; rep++)); do
        echo ""
        echo "--- rep $rep/$REPS ---"
        # Rotate arm order every rep so a monotonic drift cannot masquerade as
        # an arm effect.
        local order=()
        for ((k = 0; k < 3; k++)); do order+=("${arms[$(((k + rep - 1) % 3))]}"); done

        for arm in "${order[@]}"; do
            canary "pre-$arm-rep$rep" > /dev/null
            local dylib=""
            case "$arm" in
                null) dylib="$WORK_DIR/dit_off.dylib" ;;
                dit)  dylib="$WORK_DIR/dit_on.dylib" ;;
            esac
            python3 "$SRC/run_one.py" --arm "$arm" --rep "$rep" \
                --speedometer-dir "$SP_DIR" --browser-bin "$BROWSER_BIN" \
            --browser-kind "$BROWSER" \
                --dylib "$dylib" --iterations "$ITERATIONS" --timeout "$TIMEOUT" \
                --port "$PORT" --out "$RESULTS" --log-dir "$LOG_DIR" $sandbox_flag $EXTRA_RUN_ARGS || true
            sleep 5   # let the machine settle between arms
        done
    done
    canary "final" > /dev/null
}

report() {
    say "report"
    python3 "$SRC/summarize.py" "$WORK_DIR" "$BROWSER$VARIANT" | tee "$WORK_DIR/summary-$BROWSER$VARIANT.txt"
    echo ""
    echo "Hand these back to Claude:"
    echo "  $RESULTS"
    echo "  $CANARY_OUT"
    echo "  $WORK_DIR/summary-$BROWSER$VARIANT.txt"
}

# Prebuilt arm64 Chromium: ad-hoc/linker-signed, so unlike shipped Chrome it has
# no library-validation flag and accepts DYLD_INSERT_LIBRARIES. ~170 MB, no build.
fetch_chromium() {
    say "fetch chromium"
    local b=https://commondatastorage.googleapis.com/chromium-browser-snapshots
    local rev; rev="$(curl -sf "$b/Mac_Arm/LAST_CHANGE")" || die "cannot reach snapshot bucket"
    mkdir -p "$WORK_DIR/chromium"
    cd "$WORK_DIR/chromium"
    if [[ ! -d chrome-mac ]]; then
        echo "downloading rev $rev ..."
        curl -f -o chrome-mac.zip "$b/Mac_Arm/$rev/chrome-mac.zip" || die "download failed"
        unzip -q -o chrome-mac.zip
        echo "$rev" > REV
    fi
    echo "chromium rev $(cat REV) at $CHROMIUM"
    codesign -dv "$WORK_DIR/chromium/chrome-mac/Chromium.app" 2>&1 | grep -i flags
    cd - > /dev/null
}

case "${1:-all}" in
    fetch-chromium) fetch_chromium ;;
    setup)  setup ;;
    verify) verify ;;
    bench)  bench ;;
    report) report ;;
    all)    setup; verify; bench; report ;;
    *)      die "unknown command: $1 (setup|verify|bench|report|all)" ;;
esac
