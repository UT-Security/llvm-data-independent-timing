#!/usr/bin/env bash
#
# What does blanket PSTATE.DIT cost? One binary, two env values, many reps.
#
#   sudo -E utils/dit_blanket/run_blanket.sh
#
# Root is for kperf. Without it the run still works and reports TIME only, and
# the clock gate below cannot fire, which is the one thing worth having root
# for -- see GATE 3.
#
# ENV
#   WORK=<dir>   libsodium build tree (default ~/Documents/libsodium-arms-silicon/base)
#   BENCH=<dir>  supplies perf.c      (default ~/Documents/crypto-dit-benchmarks)
#   OUT=<dir>    results             (default ~/Documents/dit-blanket-<stamp>)
#   REPS=<n>     paired reps         (default 21)
#   ITERS=<n>    ops per rep         (default 200000)
#   PRIMS="..."  default: control + the five fast CIO primitives
#
# HOW THE EFFECT IS COUNTED
#
# The estimator is a ratio of medians, per primitive:
#
#     overhead = median(cycles_C) / median(cycles_A) - 1
#
# where A and C are the SAME executable run with BLANKET_DIT=0 and =1. Ratios
# need no clock and no offset correction, which is the point: the absolute
# cycles-per-op here is not a number anyone should quote, and the ratio is.
#
# Arms ALTERNATE within each rep (A,C,A,C,...) rather than running all of one
# then all of the other, so a thermal or scheduling drift over the session
# lands on both arms equally instead of being attributed to the mode. Medians,
# not means, so a single descheduled rep cannot move the answer.
#
# Five gates. A row that fails one is not a result:
#
#   GATE 1  INSTRUMENT.  The counters are read twice per REP, not per op, so
#           kperf's ~3,400 cycles and ~17,700 instructions are divided by ITERS.
#           The gate asserts that pair is < 0.01% of the measured total. This is
#           the whole reason this rig exists: per-region reads cost more than a
#           275-cycle AES-GCM operation and compress every percentage.
#
#   GATE 2  SAME WORK.  instructions/op must agree between arms to within 0.05%.
#           One binary, one input, one iteration count -- so a real difference
#           means something other than the mode changed, and the cycle ratio is
#           then meaningless. This is the gate that catches an arm that silently
#           took a different code path.
#
#   GATE 3  CLOCK.  implied GHz = cycles/ns, per arm. If the two arms ran at
#           different frequencies then time and cycles disagree and only cycles
#           mean anything. Measured on this tree: 3.44 GHz on base against 4.42
#           on an arm carrying `sb` drains, which made hardened arms look 34
#           points cheaper in TIME than they were in CYCLES. The gate reports
#           both arms' clocks and fails the row if they differ by more than 1%.
#
#   GATE 4  DIT WAS ON.  `control` is a data-dependent pointer chase, precisely
#           what DIT is specified to make constant-time, so it MUST slow down.
#           If it does not, DIT never got set and every other row is a null
#           result about nothing. A readback of PSTATE.DIT at exit backs it up:
#           1 for C, 0 for A.
#
#   GATE 5  RESOLVABLE.  |median_C - median_A| must clear 3x the larger arm's
#           within-arm MAD. Otherwise the arms are not separated by more than
#           their own noise and the honest answer is "below this rig's floor",
#           not the point estimate.
#
set -uo pipefail
WORK="${WORK:-$HOME/Documents/libsodium-arms-silicon/base}"
BENCH="${BENCH:-$HOME/Documents/crypto-dit-benchmarks}"
OUT="${OUT:-$HOME/Documents/dit-blanket-$(date +%Y%m%d-%H%M%S)}"
REPS="${REPS:-21}"
ITERS="${ITERS:-200000}"
PRIMS="${PRIMS:-control ed25519_sign ed25519_open chacha_enc chacha_dec aes_enc aes_dec}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33mwarn: %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[31merror: %s\033[0m\n' "$*" >&2; exit 1; }

INC="$WORK/src/libsodium/include"
LIB="$WORK/libsodium-base.a"
[[ -d "$INC" ]] || die "libsodium headers not found: $INC"
[[ -f "$LIB" ]] || die "libsodium archive not found: $LIB"
[[ -f "$BENCH/perf.c" ]] || die "perf.c not found: $BENCH/perf.c"
mkdir -p "$OUT" || die "cannot create $OUT"

# The UNHARDENED archive, on purpose. Blanket DIT is a mode, not codegen: the
# two arms must be the same machine code or the comparison silently becomes a
# codegen comparison. Anything but libsodium-base.a here is a bug.
info "build"
clang -O2 -Wall -I"$INC" -I"$BENCH" -o "$OUT/blanket_bench" \
      "$HERE/blanket_bench.c" "$LIB" -lm 2>"$OUT/build.log" \
  || { cat "$OUT/build.log"; die "build failed"; }

{ echo "date: $(date -u +%FT%TZ)"
  echo "host: $(sysctl -n hw.model) $(sysctl -n machdep.cpu.brand_string)"
  echo "root: $([[ $(id -u) -eq 0 ]] && echo yes || echo no)"
  echo "libsodium: $LIB"
  echo "clang: $(clang --version | sed -n 1p)"
  echo "reps: $REPS  iters: $ITERS"
  echo "soak: ${SOAK:-180}s"
} > "$OUT/provenance.txt"
cat "$OUT/provenance.txt"

# ---------------------------------------------------------------- soak
# Drive the machine to its sustained-load steady state BEFORE measuring, so
# every run reports from the same place. This is not thermal superstition and it
# is not a cache flush: measured on the M5, arm A of aes_dec reads ~810 cyc/op in
# any run lasting minutes and 834-848 in one lasting seconds, a 4.5% difference
# that lands entirely on the unhardened arm and silently sets the answer. It is
# NOT the core clock -- a 400-rep run spanning 4.607 to 4.391 GHz held arm A flat
# at 809-810 the whole way (r = -0.169 against the ratio). Whatever saturates, it
# saturates within a few minutes of load, so the fix is to get there first rather
# than to explain it.
#
# SOAK=0 skips it, which is what you want when comparing against a run that had
# no soak -- an unsoaked number and a soaked one are not comparable.
SOAK="${SOAK:-180}"
if [[ "$SOAK" != 0 ]]; then
  info "soak ${SOAK}s (steady state before the first measurement)"
  soak_end=$(( $(date +%s) + SOAK ))
  while [[ $(date +%s) -lt $soak_end ]]; do
    BLANKET_DIT=0 "$OUT/blanket_bench" aes_enc 400000 0 >/dev/null 2>&1
  done
fi

echo "primitive,arm,iters,cycles,instructions,ticks,dit_exit" > "$OUT/blanket.csv"
for p in $PRIMS; do
  info "$p  ($REPS paired reps x $ITERS ops)"
  for rep in $(seq 1 "$REPS"); do
    # Alternating AND rotating. Alternating alone is not enough: with a fixed
    # `0 1` order arm A always follows whatever ran before it and arm C always
    # follows A, so any carryover lands on one arm systematically. Measured, and
    # this is why the rotation is here: running `control` immediately before
    # aes_dec left A at 174.6 ns against 182.1 ns when aes_dec ran alone, while C
    # did not move at all -- turning a +0.24% result into +4.18% with no change
    # to the mode. Swapping the order on even reps cancels it.
    order="0 1"; (( rep % 2 == 0 )) && order="1 0"
    for arm in $order; do
      BLANKET_DIT="$arm" "$OUT/blanket_bench" "$p" "$ITERS" $((ITERS/10)) \
        2>>"$OUT/run.log" >> "$OUT/blanket.csv" || warn "$p arm=$arm rep=$rep failed"
    done
  done
done

info "report"
OUT="$OUT" python3 "$HERE/blanket_report.py"
echo
info "results in $OUT"
