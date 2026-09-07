#!/bin/bash
# Reproduce experiment 14 (AWS-LC: the bracket Amazon ships, on Amazon's own benchmark) on an
# Apple Silicon Mac whose kernel exposes the PMCs to user mode (pmc stage) and the thread-bind
# sysctl (enable_skstb=1). Root is used for one thing: the run stage's driver, for the bind.
#
#   ./reproduce.sh            every stage, in order
#   ./reproduce.sh 3          the same, with the run stage repeated 3 times: each run's JSON is kept in
#                             results-<host>/raw/run-N/, and raw/speed.json becomes the per-cell MEAN across
#                             runs (aggregate_awslc.py), which collect, analyze and the charts then use
#   ./reproduce.sh 3 paper    three runs of the paper stage
#   ./reproduce.sh pmc        gate: utils/cio_pmc_check.c must report PMC access available
#   ./reproduce.sh build      AWS-LC v5.8.0 from GitHub, three bssl builds, libditctl.dylib  (~6 min)
#   ./reproduce.sh run        six arms x ten test filters x (1 warm-up + 7 reps), 400 ms windows  (~40 min, idle machine)
#                             asks for sudo: the hard bind is a kern.* sysctl write, root only; the
#                             driver alone runs as root and the results are chowned back afterwards
#   ./reproduce.sh collect    raw run results + provenance -> results-<host>/raw/ (results-m4/ here); build diffs and counts -> data/
#   ./reproduce.sh analyze    report.md, summary.json, paper_table.{md,csv}, geomeans and the bar charts into
#                             results-<host>/summary/, the page into figures/
#   ./reproduce.sh paper      ONLY the paper's ten rows: run with BENCH_TESTS and CHUNKS restricted to what produces
#                             them (about 20 minutes at 400 ms), then collect and analyze, and print the table
#
# Before the run stage: a quiet-machine check (CPU in use right now across all processes, the busiest
# of them, user-facing applications still open; refuses above 60% of a core unless FORCE=1), and Spotlight
# indexing is switched off for the duration inside the sudo session and switched back on after
# (SPOTLIGHT_RESTORE=0 leaves it off).
#
# Env: RUNS (how many times the run stage repeats; a bare integer among the arguments sets it),
#      W (work dir, default ~/Documents/dit-awslc), PIN_CPU (9, a P-core: hard bind via
#      kern.sched_thread_bind_cpu, needs the enable_skstb=1 kernel), REPS/WARM (7/1), TIMEOUT_MS (400),
#      HOST_TAG (results folder suffix; default from the CPU brand, e.g. m4),
#      CHUNKS (16,256,1350,8192,16384), BENCH_TESTS, BENCH_ARMS, CC_BIN/CXX_BIN (cc/c++), JOBS
set -euo pipefail
E="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
R="$(cd "$E/../.." && pwd)"
RIG="$R/utils/dit_host_screening/awslc"
export W="${W:-$HOME/Documents/dit-awslc}" PIN_CPU="${PIN_CPU:-9}"   # -> DITCTL_PIN_CPU for utils/cio_ditctl.c
HOST_TAG="${HOST_TAG:-$(sysctl -n machdep.cpu.brand_string 2>/dev/null | tr 'A-Z ' 'a-z-' | sed 's/^apple-//')}"
RES="$E/results-${HOST_TAG:-unknown}"
# a bare integer among the arguments is the run count; the rest are stages
RUNS="${RUNS:-1}"; ARGS=()
for a in "$@"; do if [[ "$a" =~ ^[0-9]+$ ]]; then RUNS="$a"; else ARGS+=("$a"); fi; done
STAGES="${ARGS[*]:-pmc build run collect analyze}"
if [[ " $STAGES " == *" paper "* ]]; then
  # the paper stage is run+collect+analyze with the filters and sizes that yield exactly the ten rows
  export BENCH_TESTS="${BENCH_TESTS:-AES-128,AEAD-ChaCha20-Poly1305,ECDSA P-256,RNG}" CHUNKS="${CHUNKS:-16,1350,16384}"
  STAGES="${STAGES/paper/run collect analyze paper}"
fi
want() { [[ " $STAGES " == *" $1 "* ]]; }
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
[[ "$(uname -s)/$(uname -m)" == Darwin/arm64 ]] || die "experiment 14 runs on Apple Silicon (PSTATE.DIT, macOS)"
[[ $EUID -ne 0 ]] || die "run this as your user: only the driver needs root, and the run stage takes sudo itself"
[[ "$(sysctl -n kern.sched_thread_bind_cpu 2>/dev/null)" != "" ]] || die "kern.sched_thread_bind_cpu is absent: this kernel cannot hard-pin (needs enable_skstb=1)"
mkdir -p "$W"

if want pmc; then
  info "pmc gate"; clang -O1 -o "$W/pmc_check" "$R/utils/cio_pmc_check.c" || die "cannot build cio_pmc_check"
  "$W/pmc_check" || die "PMC access is not available on this kernel; the speed rows would carry zero cycles"
fi
if want build; then
  # a full build re-copies and re-patches the trees and rebuilds everything (minutes, all cores); with the
  # three binaries present only the gates run, unless REBUILD=1
  if [[ "${REBUILD:-0}" != 1 && -x "$W/build-rel/tool/bssl" && -x "$W/build-dit/tool/bssl" && -x "$W/build-ditsb/tool/bssl" ]]; then
    info "build: the three bssl binaries exist, checking gates only (REBUILD=1 to rebuild)"; "$RIG/build_awslc.sh" build
  else
    info "build"; "$RIG/build_awslc.sh" all
  fi
fi
quiet_check() {
  # current CPU use, not the load average: the load average lags by minutes and our own build stage
  # leaves it above 3 for a while after nothing is running any more
  local load busy; load=$(sysctl -n vm.loadavg | awk '{print $2}')
  busy=$(ps -Aro pcpu= | awk '{s+=$1} END{printf "%.0f", s}')
  info "quiet-machine check: CPU in use now ${busy}% of one core across all processes (1-minute load $load, informational)"
  echo "    busiest processes:"; ps -Aro pcpu,comm | sed -n '2,7p' | sed -E 's|/Applications/||; s|.*/([^/]+)\.app/Contents/MacOS/.*|\1|' | sed 's/^/      /'
  local apps; apps=$(ps -Ao comm | grep -E '\.app/Contents/MacOS/' | sed -E 's|.*/([^/]+)\.app/Contents/MacOS/.*|\1|' | sort -u \
      | grep -E '^(Slack|Safari|Messages|Calendar|Activity Monitor|Mail|Music|Spotify|Zoom|Discord|Notes|Xcode|Google Chrome|Firefox|Photos|Preview)$' | tr '\n' ' ')
  [[ -n "$apps" ]] && echo "    user applications open: $apps"
  pgrep -x htop >/dev/null && echo "    htop is running (it polls every second; quit it)"
  if (( busy > 60 )); then
    [[ "${FORCE:-0}" == 1 ]] || die "other processes are using ${busy}% of a core right now: close what is running, or FORCE=1 to measure anyway"
  fi
  echo "    spotlight: $(mdutil -s / 2>/dev/null | sed -n 2p | tr -d '\t')"
}

if want run; then
  quiet_check
  # the injected library must match the driver's protocol (DITCTL_PIN_CPU, pinned= in the exit line):
  # rebuild it from the current source every run, a one-second compile, so a stale copy cannot
  # silently turn a pinned run into a cluster-bound one (it did once: 2026-09-07)
  info "libditctl.dylib from utils/cio_ditctl.c"; cc -O2 -dynamiclib -o "$W/libditctl.dylib" "$R/utils/cio_ditctl.c" || die "libditctl build failed"
  info "run (sudo for the driver: kern.sched_thread_bind_cpu is a root-only write)"
  mkdir -p "$W/results" 2>/dev/null || true
  # one sudo session for the whole stage (the credential cache would expire during a 20-minute
  # run); the chown back to the invoking user happens inside it, so nothing root-owned is left
  rm -rf "$W/results/run-"*; mkdir -p "$W/results"; : > "$W/results/driver-stderr.log"; echo "reproduce $(date '+%F %T') RUNS=$RUNS stages: $STAGES" >> "$W/results/driver-stderr.log"
  for ((i = 1; i <= RUNS; i++)); do
    [[ $RUNS -gt 1 ]] && info "run $i of $RUNS"
    sudo -E env PATH="$PATH" HOME="$HOME" W="$W" PIN_CPU="$PIN_CPU" REPO="$R" REPS="${REPS:-7}" WARM="${WARM:-1}" \
        TIMEOUT_MS="${TIMEOUT_MS:-400}" CHUNKS="${CHUNKS:-16,256,1350,8192,16384}" ${BENCH_TESTS:+BENCH_TESTS="$BENCH_TESTS"} ${BENCH_ARMS:+BENCH_ARMS="$BENCH_ARMS"} PY="$(command -v python3)" RIG="$RIG" ME="$(id -un)" \
        SPOTLIGHT_RESTORE="${SPOTLIGHT_RESTORE:-1}" \
        bash -c 'perl -e "alarm 60; exec @ARGV" mdutil -i off / >/dev/null 2>&1 && echo "    spotlight indexing off (root volume) for the run";
                 mkdir -p "$W/results"; "$PY" "$RIG/bench_awslc.py" 2> >(tee -a "$W/results/driver-stderr.log" >&2) | tee "$W/results/speed.txt"; rc=${PIPESTATUS[0]}; chown -R "$ME" "$W/results";
                 if [[ "$SPOTLIGHT_RESTORE" == 1 ]]; then perl -e "alarm 60; exec @ARGV" mdutil -i on / >/dev/null 2>&1 && echo "    spotlight indexing back on"; fi; exit $rc' \
      || die "run $i failed"
    grep -E '^pinned|^gate' "$W/results/speed.txt"
    mkdir -p "$W/results/run-$i"; cp "$W/results/speed.txt" "$W/results/speed.json" "$W/results/run-$i/"
  done
  # raw/speed.json is the per-cell mean across runs (for one run: that run, with the run record attached)
  python3 "$RIG/aggregate_awslc.py" "$W/results/speed.json" "$W"/results/run-*/speed.json || die "aggregation failed"
fi
if want collect; then
  info "collect -> $RES/raw (run results) and $E/data (build record)"; mkdir -p "$RES/raw" "$E/data"
  cp "$W/results/speed.json" "$RES/raw/" 2>/dev/null || die "no results to collect"
  rm -rf "$RES"/raw/run-*; for d in "$W"/results/run-*/; do [[ -d "$d" ]] && cp -R "$d" "$RES/raw/"; done
  cp "$W/results/run-1/speed.txt" "$RES/raw/speed.txt" 2>/dev/null || cp "$W/results/speed.txt" "$RES/raw/speed.txt" 2>/dev/null || true
  cp "$W/switch_counts.txt" "$E/data/switch_counts.txt"
  cp "$W/bracket_sites.txt" "$E/data/bracket_sites.txt" 2>/dev/null || true
  diff -u "$W/src/aws-lc-5.8.0/tool/speed.cc" "$W/tree-rel/tool/speed.cc" | sed '1s|.*|--- aws-lc-5.8.0/tool/speed.cc|; 2s|.*|+++ tool/speed.cc (PMC patch)|' > "$E/data/speed_pmc.diff" || true
  for v in ditsb; do
    diff -u "$W/src/aws-lc-5.8.0/crypto/fipsmodule/cpucap/cpu_aarch64.c" "$W/tree-$v/crypto/fipsmodule/cpucap/cpu_aarch64.c" \
      | sed "1s|.*|--- aws-lc-5.8.0/crypto/fipsmodule/cpucap/cpu_aarch64.c|; 2s|.*|+++ cpu_aarch64.c ($v)|"; done > "$E/data/variants.diff" || true
  {
    echo "== $(date '+%Y-%m-%d %H:%M %Z')  host $(sysctl -n hw.model) $(sysctl -n machdep.cpu.brand_string)  macOS $(sw_vers -productVersion)"
    echo "repo: $(git -C "$R" rev-parse HEAD)$(git -C "$R" diff --quiet || echo ' +uncommitted')  compiler: $(${CC_BIN:-cc} --version | head -1)"
    echo "aws-lc: v5.8.0 from $(shasum -a 256 "$W/src/aws-lc-v5.8.0.tar.gz" | cut -c1-16)...  builds: $(tr '\n' ';' < "$W/switch_counts.txt")"
    echo "pmc: $("$W/pmc_check" 2>/dev/null | grep -E 'VERDICT|read cost' | tr '\n' ' ')"
    # reps, window and chunks come from the run's own JSON, not from this shell's environment
    echo "runs: $(python3 -c "import json;d=json.load(open('$W/results/speed.json'));print(d.get('runs_count', 1))") (per-cell mean across runs in raw/speed.json; each run in raw/run-N/)"
    echo "pin_cpu: $(python3 -c "import json;d=json.load(open('$W/results/speed.json'));print(d.get('pin_cpu'))") (kern.sched_thread_bind_cpu; boot-args: $(sysctl -n kern.bootargs 2>/dev/null | tr ' ' '\n' | grep skstb))  $(python3 -c "import json;d=json.load(open('$W/results/speed.json'));print(f\"reps {d.get('reps')}  timeout_ms {d.get('timeout_ms')}  chunks {d.get('chunks')}  flagged {d.get('flagged')}  unpinned {d.get('unpinned')}\")")"
  } >> "$RES/raw/provenance.txt"
  tail -5 "$RES/raw/provenance.txt"
fi
if want analyze; then
  info "analyze -> $RES/summary/, $E/figures/shipping-bracket.html"; mkdir -p "$E/figures" "$RES/summary"
  python3 "$RIG/analyze_awslc.py" "$RES/raw/speed.json" --out "$RES/summary" --html "$E/figures/shipping-bracket.html" --provenance "$RES/raw/provenance.txt"
  "${MPL:-python3}" "$RIG/plot_awslc.py" "$RES/summary/summary.json" --out "$RES/summary" && cp "$RES/summary/paper_rows.png" "$E/figures/paper_rows.png"
fi
if want paper; then info "the paper's ten rows"; cat "$RES/summary/paper_table.md"; fi
info "done: $STAGES"
