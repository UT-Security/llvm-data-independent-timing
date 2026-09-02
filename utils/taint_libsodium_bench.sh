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
# see docs/results/dit-cost-model.md before interpreting any result here.
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
#   PIN_CPU=<n>       logical CPU for the hard bind  (default 7)
#
# CORE RESIDENCY (2026-09-01). The harness used to inject libcpupin.dylib, which
# writes the sysctl kern.sched_thread_bind_cpu. That OID only exists on a kernel
# booted with enable_skstb=1; on stock macOS the write fails with ENOENT, the
# error goes to stderr which this script discards, and the run proceeds UNPINNED
# while still printing "pin_cpu=7". Measured on Mac17,2: the OID is absent.
# libqospin.dylib replaces it -- it still tries the hard bind, then falls back to
# QOS_CLASS_USER_INTERACTIVE, the supported P-cluster bias on Apple silicon.
# That is a bias, not a binding, so the ditprobe driver reports the measured core
# clock per rep (P ~4590 MHz, E ~1045 MHz) and the run is only valid if every arm
# reads P-core clock. Include ditprobe in BENCHES to arm that gate.
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
# Override for a one-off study, e.g. the CIO-parity case study:
#   VARIANTS="A:baseline:0 C:baseline:1 P:hardened:0 F:func:0 X:fine:0"
VARIANTS="${VARIANTS:-A:baseline:0 B:hardened:0 D:tuned:0 E:func:0 C:baseline:1}"

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
echo "reps=$REPS  qos=user-interactive (hard bind to cpu=$PIN if the kernel has it)"
echo "benches=$BENCHES"
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

  # Build every variant FIRST, then interleave the runs (rep outer, variant inner).
  #
  # This ordering is load-bearing. With variant as the outer loop, configuration is
  # confounded with wall-clock time: on argon2id (~9 min per config) the machine warms
  # up across a 45-min run, so whichever config ran first wins. Measured 2026-08-05 --
  # baseline's own 5 samples drifted 271.7 -> 281.3 ms (3.5%) monotonically, as large
  # as the entire between-config spread (4.2%), manufacturing a bogus ~1.04x for every
  # hardened variant. Round-robin spreads any drift evenly across configs instead.
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
  done

  # Rotate the arm order every rep. Round-robin alone still leaves each variant in a
  # FIXED position within a rep, so rep 1's cold slot always lands on the same arm --
  # the residual flagged in docs/results/dit-cost-model.md. Rotating by one each rep
  # gives every arm the cold slot equally often.
  read -r -a VARR <<< "$VARIANTS"
  NV=${#VARR[@]}
  for r in $(seq 1 "$REPS"); do
    for k in $(seq 0 $((NV - 1))); do
      v=${VARR[$(( (k + r - 1) % NV ))]}
      label=${v%%:*}; rest=${v#*:}; arch=${rest%:*}; dit=${rest#*:}
      bin="$OUT/$b.$arch.$dit"
      [[ -x "$bin" ]] || continue
      # Driver output is  '=== <key words> <result> <mem_usage>'. The key is not
      # always one word (x25519 prints '=== Key Agreement'), so take the result as
      # $(NF-1) and join everything between '===' and it. The old '$2 $3' form fed
      # the literal string 'Agreement' to float() and killed the whole bench.
      ENABLE_DIT="$dit" PIN_CPU="$PIN" DYLD_INSERT_LIBRARIES="$BENCH_DIR/libqospin.dylib" \
        "$bin" 2>/dev/null \
        | awk -v L="$label" '/^=== / && NF>=4 {k=""; for(i=2;i<=NF-2;i++) k=k (i>2?"_":"") $i;
                             print L, k, $(NF-1)}' >> "$OUT/$b.raw"
    done
  done

  # MEDIAN is the primary statistic, not min-of-N. min assumes noise only ever makes a
  # run slower, so the fastest sample is the cleanest -- false here: on 2026-08-05
  # argon2id's very first process launch came in 273.0 ms against a 280-284 ms steady
  # state, so min latched onto a cold-start outlier and reported a spurious 1.026x for
  # every hardened variant. Median ignores it and gives +-0.2%.
  BENCH="$b" VARS="$VARIANTS" python3 - "$OUT/$b.raw" <<'PY'
import os, sys, statistics as st
bench, vars_ = os.environ['BENCH'], os.environ['VARS'].split()
labels = [v.split(':')[0] for v in vars_]
data = {}
order = []
for ln in open(sys.argv[1]):
    p = ln.split()
    if len(p) < 3: continue
    lab, key, val = p[0], p[1], float(p[2])
    data.setdefault((key, lab), []).append(val)
    if key not in order: order.append(key)
for key in order:
    med = {c: st.median(data[(key, c)]) for c in labels if (key, c) in data}
    if 'A' not in med: continue
    base = med['A']
    # A metric can legitimately read 0 in the control arm (the DitBit readback
    # row), which makes a ratio undefined -- print the absolutes and move on.
    ratios = ("   |" + "".join(f" {med[c]/base:7.3f}x"
                               for c in labels if c != 'A' and c in med)) if base else ""
    print(f"{bench:<24} {key:<9}" + "".join(f" {med.get(c,0):10.0f}" for c in labels) + ratios)
    # Noise floor: worst within-config spread vs the between-config range of medians.
    # Comparable magnitudes mean the ratios are not resolvable at this sample count.
    spreads = [(max(v) - min(v)) / min(v) for c, v in
               ((c, data[(key, c)]) for c in labels if (key, c) in data) if min(v) > 0]
    worst = max(spreads) if spreads else 0.0
    lo = min(med.values()) if med else 0.0
    # A key can legitimately read 0 in some arms (the DitBit readback row), which
    # makes a relative spread undefined -- report it as unresolvable, not a crash.
    betw = (max(med.values()) - lo) / lo if lo > 0 else 0.0
    flag = "   <-- NOT RESOLVABLE, ratios above are noise" if worst >= 0.5 * betw else ""
    print(f"{'':<24} {'(noise)':<9} within-config spread {worst*100:.1f}%"
          f"   between-config {betw*100:.1f}%{flag}")
PY
done

cat <<EOF

MEDIAN of $REPS reported (not min -- min latches onto cold-start outliers; see the
argon2id note in docs/results/dit-cost-model.md). Runs are interleaved round-robin so
thermal drift hits every configuration equally.
Units = cntvct_el0 ticks/op = NANOSECONDS here (cntfrq_el0 is 1 GHz on M4, though the
counter advances in 41 ns steps). It measures TIME, not cycles: it is DVFS-independent,
so ratios are not frequency-normalized -- pin to a P-core and prefer sudo for kperf.
Raw per-rep samples: $OUT/*.raw

Reading the result: if C/A ~ 1.00 the workload is DIT-INSENSITIVE, blanket DIT is
free, and every taint-driven policy is pure overhead -- that is a property of the
workload, not a refutation of the approach. Fine-grained placement only pays where
dwell is real (LVP-heavy code, some SPEC 2026). See docs/results/dit-cost-model.md.
EOF
