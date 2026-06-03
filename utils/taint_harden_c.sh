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
  --opt-level LEVEL      LLVM optimization level. Default: -O2
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
OPT_LEVEL="-O2"
DO_LINK=1
LINK_ARGS=()
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
HARDENED_SOURCE="${PREFIX}.hardened_source.c"
if [[ -z "${EXE_OUT}" ]]; then
  EXE_OUT="${PREFIX}.hardened"
fi

echo "[1/6] C -> LLVM IR"
"${CLANG}" -gline-tables-only "${OPT_LEVEL}" -S -emit-llvm \
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

echo "[4/6] Taint analysis + ISB insertion"
"${LLC}" -enable-new-pm -run-taint-interproc -taint-insert-isb \
  -taint-output="${TAINT_OUT}" -taint-regions-output="${REGIONS_OUT}" \
  -taint-source-regions-output="${SOURCE_REGIONS_OUT}" \
  "${PE_MIR}" -o "${HARDENED_MIR}"

generate_hardened_source_view() {
  local input_abs="$1"
  local input_base="$2"
  local input_original="$3"
  local regions_file="$4"
  local output_file="$5"

  awk -v src="${input_abs}" -v src_base="${input_base}" \
    -v src_original="${input_original}" '
    BEGIN {
      FS = "\t";
      barrier_in = "__asm__ __volatile__(\"isb sy\" ::: \"memory\");";
      barrier_out = "__asm__ __volatile__(\"dsb sy\" ::: \"memory\");";
    }
    FNR == NR {
      if ($1 != src && $1 != src_base && $1 != src_original)
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
      if (!(start in range_end) || end > range_end[start])
        range_end[start] = end;
      if ($5 == "DSB")
        range_has_exit[start] = 1;
      next;
    }
    {
      lines[++line_count] = $0;
      next;
    }
    END {
      merged_count = 0;
      for (start in range_end) {
        starts[++start_count] = start + 0;
      }
      for (i = 1; i <= start_count; ++i) {
        for (j = i + 1; j <= start_count; ++j) {
          if (starts[j] < starts[i]) {
            tmp = starts[i];
            starts[i] = starts[j];
            starts[j] = tmp;
          }
        }
      }
      for (i = 1; i <= start_count; ++i) {
        start = starts[i];
        end = range_end[start];
        has_exit = range_has_exit[start] ? 1 : 0;
        if (merged_count > 0 && start <= merged_end[merged_count]) {
          if (end > merged_end[merged_count])
            merged_end[merged_count] = end;
          if (has_exit)
            merged_exit[merged_count] = 1;
        } else {
          ++merged_count;
          merged_start[merged_count] = start;
          merged_end[merged_count] = end;
          merged_exit[merged_count] = has_exit;
        }
      }
      next_region = 1;
      for (line = 1; line <= line_count; ++line) {
        while (next_region <= merged_count && merged_start[next_region] < line)
          ++next_region;
        while (next_region <= merged_count && merged_start[next_region] == line) {
          print barrier_in;
          ++next_region;
        }
        print lines[line];
        for (region = 1; region <= merged_count; ++region) {
          if (merged_end[region] == line && merged_exit[region])
            print barrier_out;
        }
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

echo
echo "Hardened MIR: ${HARDENED_MIR}"
echo "Object:       ${OBJ}"
if [[ "${DO_LINK}" -eq 1 ]]; then
  echo "Executable:   ${EXE_OUT}"
fi
echo "Taint report: ${TAINT_OUT}"
echo "Region report: ${REGIONS_OUT}"
echo "Source region report: ${SOURCE_REGIONS_OUT}"
echo "Hardened source view: ${HARDENED_SOURCE}"
