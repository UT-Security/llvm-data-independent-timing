#!/bin/bash
# Reproduce experiment 13 (AWS-LC: the bracket Amazon ships, on Amazon's own benchmark) on an
# Apple Silicon Mac whose kernel exposes the PMCs to user mode (pmc stage) and the thread-bind
# sysctl (enable_skstb=1). Root is used for one thing: the run stage's driver, for the bind.
#
#   ./reproduce.sh            every stage, in order
#   ./reproduce.sh pmc        gate: utils/cio_pmc_check.c must report PMC access available
#   ./reproduce.sh build      AWS-LC v5.8.0 from GitHub, three bssl builds, libditctl.dylib  (~6 min)
#   ./reproduce.sh run        six arms x ten test filters x (1 warm-up + 7 reps), 50 ms windows   (~4 min, idle machine)
#                             asks for sudo: the hard bind is a kern.* sysctl write, root only; the
#                             driver alone runs as root and the results are chowned back afterwards
#   ./reproduce.sh collect    run results + provenance -> results-<host>/ (results-m4/ here); build diffs and counts -> data/
#   ./reproduce.sh analyze    report.md + summary.json into results-<host>/, the page into figures/shipping-bracket.html
#
# Env: W (work dir, default ~/Documents/dit-awslc), PIN_CPU (9, a P-core: hard bind via
#      kern.sched_thread_bind_cpu, needs the enable_skstb=1 kernel), REPS/WARM (7/1), TIMEOUT_MS (50),
#      HOST_TAG (results folder suffix; default from the CPU brand, e.g. m4),
#      CHUNKS (16,256,1350,8192,16384), BENCH_TESTS, BENCH_ARMS, CC_BIN/CXX_BIN (cc/c++), JOBS
set -euo pipefail
E="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
R="$(cd "$E/../.." && pwd)"
RIG="$R/utils/dit_host_screening/awslc"
export W="${W:-$HOME/Documents/dit-awslc}" PIN_CPU="${PIN_CPU:-9}"
HOST_TAG="${HOST_TAG:-$(sysctl -n machdep.cpu.brand_string 2>/dev/null | tr 'A-Z ' 'a-z-' | sed 's/^apple-//')}"
RES="$E/results-${HOST_TAG:-unknown}"
STAGES="${*:-pmc build run collect analyze}"
want() { [[ " $STAGES " == *" $1 "* ]]; }
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
[[ "$(uname -s)/$(uname -m)" == Darwin/arm64 ]] || die "experiment 13 runs on Apple Silicon (PSTATE.DIT, macOS)"
[[ $EUID -ne 0 ]] || die "run this as your user: only the driver needs root, and the run stage takes sudo itself"
[[ "$(sysctl -n kern.sched_thread_bind_cpu 2>/dev/null)" != "" ]] || die "kern.sched_thread_bind_cpu is absent: this kernel cannot hard-pin (needs enable_skstb=1)"
mkdir -p "$W"

if want pmc; then
  info "pmc gate"; clang -O1 -o "$W/pmc_check" "$R/utils/cio_pmc_check.c" || die "cannot build cio_pmc_check"
  "$W/pmc_check" || die "PMC access is not available on this kernel; the speed rows would carry zero cycles"
fi
if want build; then info "build"; "$RIG/build_awslc.sh" all; fi
if want run; then
  info "run (sudo for the driver: kern.sched_thread_bind_cpu is a root-only write)"
  mkdir -p "$W/results" 2>/dev/null || true
  # one sudo session for the whole stage (the credential cache would expire during a 20-minute
  # run); the chown back to the invoking user happens inside it, so nothing root-owned is left
  sudo -E env PATH="$PATH" HOME="$HOME" W="$W" PIN_CPU="$PIN_CPU" REPS="${REPS:-7}" WARM="${WARM:-1}" \
      TIMEOUT_MS="${TIMEOUT_MS:-50}" CHUNKS="${CHUNKS:-16,256,1350,8192,16384}" ${BENCH_TESTS:+BENCH_TESTS="$BENCH_TESTS"} ${BENCH_ARMS:+BENCH_ARMS="$BENCH_ARMS"} PY="$(command -v python3)" RIG="$RIG" ME="$(id -un)" \
      bash -c 'mkdir -p "$W/results"; "$PY" "$RIG/bench_awslc.py" | tee "$W/results/speed.txt"; chown -R "$ME" "$W/results"' \
    || die "run failed"
  grep -E '^pinned|^gate' "$W/results/speed.txt"
fi
if want collect; then
  info "collect -> $RES (run results) and $E/data (build record)"; mkdir -p "$RES" "$E/data"
  cp "$W/results/speed.txt" "$W/results/speed.json" "$RES/" 2>/dev/null || die "no results to collect"
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
    echo "pin_cpu: $(python3 -c "import json;d=json.load(open('$W/results/speed.json'));print(d.get('pin_cpu'))") (kern.sched_thread_bind_cpu; boot-args: $(sysctl -n kern.bootargs 2>/dev/null | tr ' ' '\n' | grep skstb))  $(python3 -c "import json;d=json.load(open('$W/results/speed.json'));print(f\"reps {d.get('reps')}  timeout_ms {d.get('timeout_ms')}  chunks {d.get('chunks')}  flagged {d.get('flagged')}  unpinned {d.get('unpinned')}\")")"
  } >> "$RES/provenance.txt"
  tail -5 "$RES/provenance.txt"
fi
if want analyze; then
  info "analyze -> $RES/report.md, $E/figures/shipping-bracket.html"; mkdir -p "$E/figures"
  python3 "$RIG/analyze_awslc.py" "$RES/speed.json" --out "$RES" --html "$E/figures/shipping-bracket.html" --provenance "$RES/provenance.txt"
fi
info "done: $STAGES"
