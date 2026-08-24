#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  utils/taint_harden_c.sh [options] path/to/input.c

Options:
  -s, --taint-src FILE   Taint source file. If omitted, the script tries:
                         <stem>_secret.txt, <prefix>_secret.txt, secret.txt
  -o, --output FILE      Output executable path. Default: <out-dir>/<stem>.hardened
  --out-dir DIR          Directory for intermediates and default executable.
                         Default: input file directory
  --opt-level LEVEL      LLVM optimization level. Default: -O0
  --clean                Delete intermediates after a successful run. With
                         linking enabled, only the executable is kept. With
                         --no-link, only the hardened object is kept.
  --keep-region-reports  With --clean, keep <stem>.tainted_regions.txt and
                         <stem>.tainted_source_regions.txt.
  --no-link              Stop after producing the hardened object file
  --link-arg ARG         Extra argument passed to the final clang link command
  -h, --help             Show this help

Environment:
  LLVM_BIN               Directory containing clang/opt/llc.
                         Default: <repo>/build/bin

Outputs:
  <stem>.ll
  <stem>.annotated.ll
  <stem>.pe.mir
  <stem>.hardened.mir
  <stem>.hardened.o
  <stem>.tainted.txt
  <stem>.tainted_regions.txt
  <stem>.tainted_source_regions.txt
  <stem>.hardened_source.c
  <stem>.tainted_src.txt
  <stem>.tainted_stats.txt
  <stem>.tainted_trace.txt
  <stem>.hardened
EOF
}

die() {
  echo "error: $*" >&2
  exit 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LLVM_BIN="${LLVM_BIN:-${REPO_ROOT}/build/bin}"

TAINT_SRC=""
OUT_DIR=""
EXE_OUT=""
OPT_LEVEL="-O0"
DO_LINK=1
CLEAN_INTERMEDIATES=0
KEEP_REGION_REPORTS=0
LINK_ARGS=()
TAINT_LLC_ARGS=()
INPUT_C=""

while [[ $# -gt 0 ]]; do
  case "$1" in
  -s|--taint-src)
    [[ $# -ge 2 ]] || die "$1 requires a file"
    TAINT_SRC="$2"
    shift 2
    ;;
  -o|--output)
    [[ $# -ge 2 ]] || die "$1 requires a file"
    EXE_OUT="$2"
    shift 2
    ;;
  --out-dir)
    [[ $# -ge 2 ]] || die "$1 requires a directory"
    OUT_DIR="$2"
    shift 2
    ;;
  --opt-level)
    [[ $# -ge 2 ]] || die "$1 requires an optimization level"
    OPT_LEVEL="$2"
    shift 2
    ;;
  --clean)
    CLEAN_INTERMEDIATES=1
    shift
    ;;
  --keep-region-reports)
    KEEP_REGION_REPORTS=1
    shift
    ;;
  --no-link)
    DO_LINK=0
    shift
    ;;
  --link-arg)
    [[ $# -ge 2 ]] || die "$1 requires an argument"
    LINK_ARGS+=("$2")
    shift 2
    ;;
  -h|--help)
    usage
    exit 0
    ;;
  -*)
    die "unknown option: $1"
    ;;
  *)
    [[ -z "${INPUT_C}" ]] || die "multiple input files provided"
    INPUT_C="$1"
    shift
    ;;
  esac
done

[[ -n "${INPUT_C}" ]] || die "missing input .c file"
[[ "${INPUT_C}" == *.c ]] || die "input must be a .c file: ${INPUT_C}"
[[ -f "${INPUT_C}" ]] || die "input file does not exist: ${INPUT_C}"

CLANG="${LLVM_BIN}/clang"
OPT="${LLVM_BIN}/opt"
LLC="${LLVM_BIN}/llc"
[[ -x "${CLANG}" ]] || die "clang not found or not executable: ${CLANG}"
[[ -x "${OPT}" ]] || die "opt not found or not executable: ${OPT}"
[[ -x "${LLC}" ]] || die "llc not found or not executable: ${LLC}"

SYSROOT_ARGS=()
if [[ "$(uname -s)" == "Darwin" ]]; then
  SDK_PATH="${SDKROOT:-}"
  if [[ -z "${SDK_PATH}" ]] && command -v xcrun >/dev/null 2>&1; then
    SDK_PATH="$(xcrun --show-sdk-path 2>/dev/null || true)"
  fi
  if [[ -n "${SDK_PATH}" ]]; then
    SYSROOT_ARGS=(-isysroot "${SDK_PATH}")
  fi
fi

SRC_DIR="$(cd "$(dirname "${INPUT_C}")" && pwd)"
SRC_FILE="$(basename "${INPUT_C}")"
STEM="${SRC_FILE%.c}"

if [[ -z "${TAINT_SRC}" ]]; then
  CANDIDATES=("${SRC_DIR}/${STEM}_secret.txt")
  if [[ "${STEM}" == *_ops ]]; then
    CANDIDATES+=("${SRC_DIR}/${STEM%_ops}_secret.txt")
  fi
  CANDIDATES+=("${SRC_DIR}/secret.txt")

  for Candidate in "${CANDIDATES[@]}"; do
    if [[ -f "${Candidate}" ]]; then
      TAINT_SRC="${Candidate}"
      break
    fi
  done
fi

[[ -n "${TAINT_SRC}" ]] || die "could not find taint source file; pass --taint-src FILE"
[[ -f "${TAINT_SRC}" ]] || die "taint source file does not exist: ${TAINT_SRC}"

if [[ -z "${OUT_DIR}" ]]; then
  OUT_DIR="${SRC_DIR}"
fi
mkdir -p "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"

PREFIX="${OUT_DIR}/${STEM}"
LL="${PREFIX}.ll"
ANNOTATED_LL="${PREFIX}.annotated.ll"
PE_MIR="${PREFIX}.pe.mir"
HARDENED_MIR="${PREFIX}.hardened.mir"
OBJ="${PREFIX}.hardened.o"
TAINT_OUT="${PREFIX}.tainted.txt"
REGIONS_OUT="${PREFIX}.tainted_regions.txt"
SOURCE_REGIONS_OUT="${PREFIX}.tainted_source_regions.txt"
TAINT_SRC_OUT="${PREFIX}.tainted_src.txt"
TAINT_STATS_OUT="${PREFIX}.tainted_stats.txt"
TAINT_TRACE_OUT="${PREFIX}.tainted_trace.txt"
HARDENED_SOURCE="${PREFIX}.hardened_source.c"
if [[ -z "${EXE_OUT}" ]]; then
  EXE_OUT="${PREFIX}.hardened"
fi

echo "[1/6] C -> LLVM IR"
"${CLANG}" -g "${OPT_LEVEL}" -S -emit-llvm \
  -fno-asynchronous-unwind-tables -fno-unwind-tables \
  "${SYSROOT_ARGS[@]}" "${INPUT_C}" -o "${LL}"

echo "[2/6] Annotate taint sources: ${TAINT_SRC}"
"${OPT}" -S "${LL}" -passes=taint-annotate \
  -taint-src="${TAINT_SRC}" -o "${ANNOTATED_LL}"

echo "[3/6] LLVM IR -> post-prologepilog MIR"
"${LLC}" "${OPT_LEVEL}" -stop-after=prologepilog \
  "${ANNOTATED_LL}" -o "${PE_MIR}"

# Work around known MIR serialization issue for CFI offsets.
perl -0pi -e 's/<mcsymbol >//g' "${PE_MIR}"

echo "[4/6] Taint analysis + PSTATE.DIT mode-switch insertion"
"${LLC}" -enable-new-pm -run-taint-interproc -taint-insert-dit \
  -taint-output="${TAINT_OUT}" -taint-regions-output="${REGIONS_OUT}" \
  -taint-source-regions-output="${SOURCE_REGIONS_OUT}" \
  "${TAINT_LLC_ARGS[@]}" \
  "${PE_MIR}" -o "${HARDENED_MIR}"

generate_hardened_source_view() {
  local input_abs="$1"
  local input_base="$2"
  local input_original="$3"
  local regions_file="$4"
  local output_file="$5"

  awk -v src="${input_abs}" -v src_base="${input_base}" \
    -v src_original="${input_original}" '
    function normalize_path(path) {
      while (gsub(/\/\.\//, "/", path)) {}
      return path;
    }
    BEGIN {
      FS = "\t";
      barrier_in = "__asm__ __volatile__(\"msr DIT, #1\" ::: \"memory\");";
      barrier_out = "__asm__ __volatile__(\"msr DIT, #0\" ::: \"memory\");";
      src_norm = normalize_path(src);
      src_original_norm = normalize_path(src_original);
    }
    FNR == NR {
      report_src = normalize_path($1);
      if (report_src != src_norm && report_src != src_base &&
          report_src != src_original && report_src != src_original_norm)
        next;
      if ($2 !~ /^[0-9]+$/ || $3 !~ /^[0-9]+$/)
        next;
      start = $2 + 0;
      end = $3 + 0;
      if (end < start) {
        tmp = start;
        start = end;
        end = tmp;
      }
      # The C source view marks the secret-dependent REGIONS. Actual DIT
      # placement is function-granularity (entry/return), so this view shows
      # where the secrets are, not literally where the MSRs are emitted.
      # It is only an approximation.
      # Multi-line ranges are often caused by inlined debug locations and can
      # span across unrelated source statements or even functions. Show exact
      # source-line regions directly instead of merging those broad spans.
      if (start != end)
        next;
      line_has_entry[start] = 1;
      if ($5 == "exit")
        line_has_exit[start] = 1;
      next;
    }
    {
      lines[++line_count] = $0;
      next;
    }
    END {
      for (line = 1; line <= line_count; ++line) {
        if (line_has_entry[line])
          print barrier_in;
        print lines[line];
        if (line_has_exit[line])
          print barrier_out;
      }
    }
  ' "${regions_file}" "${input_abs}" >"${output_file}"
}

generate_hardened_source_view "${SRC_DIR}/${SRC_FILE}" "${SRC_FILE}" \
  "${INPUT_C}" "${SOURCE_REGIONS_OUT}" "${HARDENED_SOURCE}"

echo "[5/6] Hardened MIR -> object"
"${LLC}" -start-after=prologepilog "${HARDENED_MIR}" \
  -filetype=obj -o "${OBJ}"

if [[ "${DO_LINK}" -eq 1 ]]; then
  echo "[6/6] Object -> executable"
  GC_ARG=()
  case "$(uname -s)" in
  Darwin) GC_ARG=(-Wl,-dead_strip) ;;
  Linux) GC_ARG=(-Wl,--gc-sections) ;;
  esac
  if [[ ${#LINK_ARGS[@]} -gt 0 ]]; then
    "${CLANG}" "${OBJ}" "${SYSROOT_ARGS[@]}" "${GC_ARG[@]}" \
      "${LINK_ARGS[@]}" -o "${EXE_OUT}"
  else
    "${CLANG}" "${OBJ}" "${SYSROOT_ARGS[@]}" "${GC_ARG[@]}" -o "${EXE_OUT}"
  fi
else
  echo "[6/6] Skipping link (--no-link)"
fi

if [[ "${CLEAN_INTERMEDIATES}" -eq 1 ]]; then
  KEEP_OUTPUT="${EXE_OUT}"
  if [[ "${DO_LINK}" -eq 0 ]]; then
    KEEP_OUTPUT="${OBJ}"
  fi

  for Intermediate in \
    "${LL}" \
    "${ANNOTATED_LL}" \
    "${PE_MIR}" \
    "${HARDENED_MIR}" \
    "${TAINT_OUT}" \
    "${REGIONS_OUT}" \
    "${SOURCE_REGIONS_OUT}" \
    "${TAINT_SRC_OUT}" \
    "${TAINT_STATS_OUT}" \
    "${TAINT_TRACE_OUT}" \
    "${HARDENED_SOURCE}" \
    "${OBJ}"; do
    if [[ "${Intermediate}" == "${KEEP_OUTPUT}" ]]; then
      continue
    fi
    if [[ "${KEEP_REGION_REPORTS}" -eq 1 ]] &&
       { [[ "${Intermediate}" == "${REGIONS_OUT}" ]] ||
         [[ "${Intermediate}" == "${SOURCE_REGIONS_OUT}" ]]; }; then
      continue
    fi
    if [[ "${Intermediate}" != "${KEEP_OUTPUT}" ]]; then
      rm -f "${Intermediate}"
    fi
  done
fi

echo
if [[ "${CLEAN_INTERMEDIATES}" -eq 1 ]]; then
  if [[ "${DO_LINK}" -eq 1 ]]; then
    echo "Executable:   ${EXE_OUT}"
  else
    echo "Object:       ${OBJ}"
  fi
  if [[ "${KEEP_REGION_REPORTS}" -eq 1 ]]; then
    echo "Region report: ${REGIONS_OUT}"
    echo "Source region report: ${SOURCE_REGIONS_OUT}"
  fi
  echo "Intermediates cleaned (--clean)"
else
  echo "Hardened MIR: ${HARDENED_MIR}"
  echo "Object:       ${OBJ}"
  if [[ "${DO_LINK}" -eq 1 ]]; then
    echo "Executable:   ${EXE_OUT}"
  fi
  echo "Taint report: ${TAINT_OUT}"
  echo "Region report: ${REGIONS_OUT}"
  echo "Source region report: ${SOURCE_REGIONS_OUT}"
  echo "Hardened source view: ${HARDENED_SOURCE}"
fi
