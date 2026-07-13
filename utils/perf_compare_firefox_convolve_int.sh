#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  utils/perf_compare_firefox_convolve_int.sh [options] [-- benchmark args...]

Options:
  --baseline FILE       Non-hardened executable.
                        Default: playground/firefox_convolve_int.baseline
  --hardened FILE       Hardened executable.
                        Default: playground/firefox_convolve_int.hardened
  --source FILE         Source used by --build-baseline.
                        Default: playground/firefox_convolve_int.c
  --core CPU            CPU core used for both runs. Default: 0
  -r, --repeat N        perf stat repeat count. Default: 5
  -e, --events LIST     perf events, passed to "perf stat -e LIST".
                        Default: perf stat's default event set
  --build-baseline      Build the baseline before measuring
  --opt-level LEVEL     Optimization level for --build-baseline. Default: -O2
  -h, --help            Show this help

Default benchmark args:
  --width 1024 --height 1024 --kernel 9 --iter 50 --warmup 5

Examples:
  utils/perf_compare_firefox_convolve_int.sh
  utils/perf_compare_firefox_convolve_int.sh --core 3 -r 10
  utils/perf_compare_firefox_convolve_int.sh --events cycles,instructions,branches,branch-misses
  utils/perf_compare_firefox_convolve_int.sh -- --width 512 --height 512 --iter 100
EOF
}

die() {
  echo "error: $*" >&2
  exit 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LLVM_BIN="${LLVM_BIN:-${REPO_ROOT}/build/bin}"

BASELINE="${REPO_ROOT}/playground/firefox_convolve_int.baseline"
HARDENED="${REPO_ROOT}/playground/firefox_convolve_int.hardened"
SOURCE="${REPO_ROOT}/playground/firefox_convolve_int.c"
CORE=0
REPEAT=5
EVENTS=""
BUILD_BASELINE=0
OPT_LEVEL="-O2"
BENCH_ARGS=(--width 1024 --height 1024 --kernel 9 --iter 50 --warmup 5)

while [[ $# -gt 0 ]]; do
  case "$1" in
  --baseline)
    [[ $# -ge 2 ]] || die "$1 requires a file"
    BASELINE="$2"
    shift 2
    ;;
  --hardened)
    [[ $# -ge 2 ]] || die "$1 requires a file"
    HARDENED="$2"
    shift 2
    ;;
  --source)
    [[ $# -ge 2 ]] || die "$1 requires a file"
    SOURCE="$2"
    shift 2
    ;;
  --core)
    [[ $# -ge 2 ]] || die "$1 requires a CPU number"
    CORE="$2"
    shift 2
    ;;
  -r|--repeat)
    [[ $# -ge 2 ]] || die "$1 requires a repeat count"
    REPEAT="$2"
    shift 2
    ;;
  -e|--events)
    [[ $# -ge 2 ]] || die "$1 requires an event list"
    EVENTS="$2"
    shift 2
    ;;
  --build-baseline)
    BUILD_BASELINE=1
    shift
    ;;
  --opt-level)
    [[ $# -ge 2 ]] || die "$1 requires an optimization level"
    OPT_LEVEL="$2"
    shift 2
    ;;
  -h|--help)
    usage
    exit 0
    ;;
  --)
    shift
    BENCH_ARGS=("$@")
    break
    ;;
  -*)
    die "unknown option: $1"
    ;;
  *)
    die "unexpected argument: $1; put benchmark arguments after --"
    ;;
  esac
done

[[ "${CORE}" =~ ^[0-9]+$ ]] || die "--core must be a non-negative integer"
[[ "${REPEAT}" =~ ^[0-9]+$ ]] || die "--repeat must be a positive integer"
[[ "${REPEAT}" -gt 0 ]] || die "--repeat must be a positive integer"

command -v perf >/dev/null 2>&1 || die "perf not found in PATH"
command -v taskset >/dev/null 2>&1 || die "taskset not found in PATH"

if [[ "${BUILD_BASELINE}" -eq 1 ]]; then
  CLANG="${LLVM_BIN}/clang"
  [[ -x "${CLANG}" ]] || die "clang not found or not executable: ${CLANG}"
  [[ -f "${SOURCE}" ]] || die "source file does not exist: ${SOURCE}"
  echo "[build] ${BASELINE}"
  "${CLANG}" "${OPT_LEVEL}" -g "${SOURCE}" -o "${BASELINE}"
fi

[[ -x "${BASELINE}" ]] || die "baseline executable not found or not executable: ${BASELINE}"
[[ -x "${HARDENED}" ]] || die "hardened executable not found or not executable: ${HARDENED}"

PERF_ARGS=(stat -r "${REPEAT}")
if [[ -n "${EVENTS}" ]]; then
  PERF_ARGS+=(-e "${EVENTS}")
fi

run_one() {
  local label="$1"
  local exe="$2"

  echo
  echo "== ${label} =="
  echo "core=${CORE} repeat=${REPEAT}"
  echo "${exe} ${BENCH_ARGS[*]}"
  taskset -c "${CORE}" perf "${PERF_ARGS[@]}" "${exe}" "${BENCH_ARGS[@]}"
}

run_one "baseline" "${BASELINE}"
run_one "hardened" "${HARDENED}"
