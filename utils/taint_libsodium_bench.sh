#!/usr/bin/env bash
#
# Runtime A/B evaluation of PSTATE.DIT placement policies on libsodium.
# Consumes the archives produced by utils/taint_libsodium_eval.sh.
#
# CONFIGURATIONS (identical benchmark source in every case)
#   A base      unhardened libsodium, DIT never set          unprotected reference
#   B hardened  taint-hardened, DEFAULT placement            region, switch-cyc=0, hoist=0
#   D tuned     taint-hardened, serializing-HW placement     region, switch-cyc=30, hoist=1
#   E func      taint-hardened, whole-function placement     -taint-dit-placement=function
#   C whole     unhardened + harness sets DIT across the     the coarse mitigation
#               entire measured region                       (what FLOP/Safari ships)
#
# THE THESIS UNDER TEST: a taint-driven policy (B/D/E) should cost less than blanket
# DIT (C) while protecting the same secrets. That holds only where dwell > 0. On a
# DIT-INSENSITIVE workload C is free and every toggle we add is pure loss --
# see utils/taint_dit_cost_model.md before interpreting any result here.
#
# USAGE
#   utils/taint_libsodium_bench.sh
#   REPS=10 BENCHES="ed25519" utils/taint_libsodium_bench.sh
#   sudo -E utils/taint_libsodium_bench.sh          # enables kperf cycle counters
#
# ENV
#   BENCH_DIR=<dir>   crypto-dit-benchmarks checkout (default ~/Documents/crypto-dit-benchmarks)
#   WORK=<dir>        libsodium work dir             (default ~/Documents/libsodium-1.0.21)
#   LLVM_BIN=<dir>    built tools                    (default <repo>/build/bin)
#   BENCHES=<list>    default "ed25519 aead_chacha20poly1305"
#   REPS=<n>          repetitions, min is reported   (default 5)
#   PIN_CPU=<n>       P-core to pin to               (default 7)
#
# NOTE ON argon2id: it is CIO's headline worst case (27.84x) but the harness runs
# 1000 iterations of a memory-hard KDF, which takes hours. Add it explicitly and
# expect a long run:  BENCHES="argon2id" REPS=1 ...
#
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LLVM_BIN="${LLVM_BIN:-$REPO_ROOT/build/bin}"
WORK="${WORK:-$HOME/Documents/libsodium-1.0.21}"
BENCH_DIR="${BENCH_DIR:-$HOME/Documents/crypto-dit-benchmarks}"
OUT="${OUT:-$BENCH_DIR/taint_ab_results}"
REPS="${REPS:-5}"
BENCHES="${BENCHES:-ed25519 aead_chacha20poly1305}"
PIN="${PIN_CPU:-7}"
INC="$WORK/src/libsodium/include"

# label:archive:harness_DIT
VARIANTS="A:baseline:0 B:hardened:0 D:tuned:0 E:func:0 C:baseline:1"

die() { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

[[ -d "$BENCH_DIR" ]] || die "benchmark dir not found: $BENCH_DIR (set BENCH_DIR=)"
[[ -f "$INC/sodium.h" ]] || die "libsodium headers not found: $INC (run taint_libsodium_eval.sh)"
for v in $VARIANTS; do
  rest=${v#*:}; a="$WORK/libsodium-${rest%:*}.a"
  [[ -f "$a" ]] || die "missing archive $a -- run: utils/taint_libsodium_eval.sh"
done
mkdir -p "$OUT"

if [[ "$(id -u)" -ne 0 ]]; then
  printf '\033[33mnote: not root -- kperf cycle counters unavailable, using cntvct_el0\n'
  printf '      (ratios are valid; absolute cycles are not). Re-run with sudo -E for cycles.\033[0m\n'
fi

echo "libsodium: $WORK"
echo "reps=$REPS  pin_cpu=$PIN  benches=$BENCHES"
printf '\n%-24s %-9s' benchmark metric
for v in $VARIANTS; do printf ' %10s' "${v%%:*}"; done
printf '   |'
for v in $VARIANTS; do l=${v%%:*}; [[ $l == A ]] || printf ' %8s' "$l/A"; done
printf '\n'

cleanup() { [[ -n "${PATCHED:-}" && -f "$PATCHED" ]] && rm -f "$PATCHED"; }
trap cleanup EXIT

for b in $BENCHES; do
  [[ -f "$BENCH_DIR/$b/$b.c" ]] || { echo "skip $b (no source)" >&2; continue; }

  # ITERS/WARMUP override. The drivers hardcode num_runs=1000 / warmup=100 as
  # locals, which is infeasible for argon2id: its loops are nested, giving
  # 66*num_runs hashes at OPSLIMIT/MEMLIMIT_MODERATE (~0.7s each) = ~12h PER
  # configuration. Patch a sibling copy -- sibling so the driver's
  # #include "../utils.h" / "../perf.c" still resolve -- and remove it after.
  SRC="$BENCH_DIR/$b/$b.c"; PATCHED=""
  if [[ -n "${ITERS:-}" || -n "${WARMUP:-}" ]]; then
    PATCHED="$BENCH_DIR/$b/.taint_iters_$b.c"
    # sed -E: BSD sed has no \+ in BRE, and would silently substitute nothing.
    sed -E -e "s/(num_runs *= *)[0-9]+/\1${ITERS:-1000}/" \
           -e "s/(warmup_runs *= *)[0-9]+/\1${WARMUP:-100}/" "$SRC" > "$PATCHED"
    grep -q "num_runs = ${ITERS:-1000}" "$PATCHED" \
      || { rm -f "$PATCHED"; echo "ERROR: $b iteration override did not apply" >&2; exit 1; }
    SRC="$PATCHED"
    echo "note: $b iteration override -> num_runs=${ITERS:-1000} warmup=${WARMUP:-100}"
  fi

  : > "$OUT/$b.raw"
  for v in $VARIANTS; do
    label=${v%%:*}; rest=${v#*:}; arch=${rest%:*}; dit=${rest#*:}
    bin="$OUT/$b.$arch.$dit"
    clang -Wall -O2 -I"$BENCH_DIR" -I"$INC" -o "$bin" "$SRC" \
          "$WORK/libsodium-$arch.a" -lm 2>/dev/null \
      || { echo "$b/$label: build FAILED" >&2; continue; }

    # Guard against measuring the wrong binary: a hardened variant with no library
    # switches means the archive did not get linked.
    if [[ "$arch" != baseline ]]; then
      nd=$("$LLVM_BIN/llvm-objdump" -d "$bin" 2>/dev/null | grep -cE 'msr[[:space:]]+DIT' | head -1 | tr -dc '0-9')
      [[ "${nd:-0}" -lt 10 ]] && echo "WARNING: $b/$label has only ${nd:-0} msr DIT" >&2
    fi

    for _ in $(seq 1 "$REPS"); do
      ENABLE_DIT="$dit" PIN_CPU="$PIN" DYLD_INSERT_LIBRARIES="$BENCH_DIR/libcpupin.dylib" \
        "$bin" 2>/dev/null | awk -v L="$label" '/^=== /{print L, $2, $3}' >> "$OUT/$b.raw"
    done
  done

  awk -v bench="$b" -v vars="$VARIANTS" '
    { key=$2; v=$3+0
      if (!((key,$1) in m) || v < m[key,$1]) m[key,$1]=v
      if (!(key in seen)) { seen[key]=1; order[++n]=key } }
    END{ nv=split(vars,V," ")
      for(i=1;i<=n;i++){ k=order[i]
        printf "%-24s %-9s", bench, k
        for(j=1;j<=nv;j++){ split(V[j],p,":"); printf " %10d", m[k,p[1]] }
        printf "   |"
        a=m[k,"A"]
        for(j=1;j<=nv;j++){ split(V[j],p,":"); if(p[1]!="A")
          printf " %7.3fx", (a? m[k,p[1]]/a : 0) }
        printf "\n" } }' "$OUT/$b.raw"
done

cat <<EOF

min-of-$REPS reported; units = cntvct_el0 ticks per operation (RATIOS are the signal).
Raw per-rep samples: $OUT/*.raw

Reading the result: if C/A ~ 1.00 the workload is DIT-INSENSITIVE, blanket DIT is
free, and every taint-driven policy is pure overhead -- that is a property of the
workload, not a refutation of the approach. Fine-grained placement only pays where
dwell is real (LVP-heavy code, some SPEC 2026). See utils/taint_dit_cost_model.md.
EOF
