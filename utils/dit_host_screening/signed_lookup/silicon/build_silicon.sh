#!/usr/bin/env bash
#
# Experiment 02 on real silicon: build the signed-lookup crossover arms for an
# Apple M-series Mac.
#
# THE POINT OF THIS RIG. paper_experiments/02 measures the secret-fraction
# crossover under gem5, on a Neoverse-V2 with EVES and VTAGE -- a value
# predictor far more capable than anything shipping. Blanket DIT's whole cost
# there is load-value predictions the mode switches off, so the obvious question
# is whether the crossover is an artifact of a research predictor. An Apple M4
# has a load value predictor too, but a CONSTANT one: it learns loads that
# return the same value at the same PC. The lane the gem5 driver already uses is
# aimed exactly at that -- on q of its iterations it reads a record header whose
# value never changes -- so the same source, unmodified, answers the question on
# hardware. Same driver, same library, same seeds; different machine.
#
#   base       unhardened libsodium. Also the BLANKET arm at run time
#              (--blanket), so those two are one binary and no instruction in
#              the measured region differs between them.
#   taint      libsodium -ftaint-harden at the shipped defaults (callee
#              contract, DIT twins, contract fixpoint seeds, owned list) --
#              ExpeDITe. On Apple silicon `msr DIT` is serialising, so this arm
#              IS the gem5 figure's "serialised" curve; the renamed curve is the
#              counterfactual only the simulator can run.
#   taintnop   identical placement and instruction count at identical addresses,
#              every `msr DIT` emitted as HINT #0. The layout control:
#              (taint - taintnop) is DIT's real cost, the rest is code motion.
#
# The library arms come from utils/taint_libsodium_arms.sh, which is already the
# Apple-silicon counterpart of the gem5 rig's build_arms.sh and is kept in sync
# with it. This script only adds the driver link, so there is exactly one place
# where an arm's CFLAGS are written down.
#
# THE DRIVER IS THE GEM5 DRIVER, byte for byte. signed_lookup_gem5.c here is a
# copy of benchmarks/signed_lookup/signed_lookup_gem5.c from gem5-DIT at the
# commit this repo pins; the sha256 is checked below, so "same program on both
# instruments" is a checkable claim and not an assertion. It includes
# "kperf_ipc.h" with a QUOTED include, which resolves to the two-line shim next
# to it and thence to pmc_ipc.h -- the PMC-register instrument. That is the only
# difference between what this builds and what gem5 runs.
#
# USAGE
#   utils/dit_host_screening/signed_lookup/silicon/build_silicon.sh          # all
#   utils/dit_host_screening/signed_lookup/silicon/build_silicon.sh link     # relink only
#
# ENV
#   LLVM_BIN=<dir>    toolchain bin/     (default <repo>/build/bin)
#   ARMS_WORK=<dir>   library build root (default ~/Documents/libsodium-arms-m4)
#   OUT=<dir>         where binaries go  (default <this dir>/bin)
#   ARMS=<list>       default "base taint taintnop"
#   LANES=<list>      default "wide narrow" (see below)
set -uo pipefail

D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$D/../../../.." && pwd)"
LLVM_BIN="${LLVM_BIN:-$REPO/build/bin}"
ARMS_WORK="${ARMS_WORK:-$HOME/Documents/libsodium-arms-m4}"
OUT="${OUT:-$D/bin}"
ARMS="${ARMS:-base taint taintnop}"
# THE TWO LANES. Same source, same code, ONE constant apart: the value every
# record header holds. A value predictor stores a finite number of value bits,
# and this constant is what decides whether the machine under test can hold it.
#
#   wide    HDR_CONST = 0x2545F4914F6CDD1D, 62 bits. What every gem5 sweep in
#           paper_experiments/02 ran. gem5's EVES/VTAGE predicts it.
#   narrow  HDR_CONST = 0xCAFEBABE, 32 bits. What an Apple M4 can predict --
#           measured on this driver, the boundary is exactly 36 bits (36 is
#           predicted, 37 is not), so any value up to 0xF_FFFF_FFFF behaves
#           identically and 0xCAFEBABE is one that looks like a type tag.
#
# Running BOTH is the experiment. On the wide lane an M4 reads blanket DIT at
# +0.0% and gem5 reads +31%; on the narrow lane the M4 reads +34.5% and tracks
# gem5's own q sweep point for point. The lane is not a tuning knob, it is the
# axis the two machines differ on.
LANES="${LANES:-wide narrow}"
#
# A third family, `bN`, exists only for the width sweep: a header of N one-bits,
# one binary per N, so the sweep varies how many value bits the predictor is
# asked to hold and nothing else. run_crossover_m4.py knows the same naming.
lane_hdr() {
  case "$1" in
    wide)   echo "0x2545F4914F6CDD1DULL" ;;
    narrow) echo "0xCAFEBABEULL" ;;
    b[0-9]|b[0-9][0-9])
            # N one-bits. bash arithmetic is signed 64-bit, so N=64 would
            # overflow; python is not available on every host this runs on and
            # printf is, so shift 63 at most and special-case the top bit.
            n="${1#b}"
            [[ "$n" -ge 1 && "$n" -le 63 ]] || die "lane $1: N must be 1..63"
            printf '0x%XULL\n' "$(( (1 << n) - 1 ))" ;;
    *) die "unknown lane: $1 (wide | narrow | bN)" ;;
  esac
}
CC="$LLVM_BIN/clang"

# The gem5-DIT revision this repo pins, and the sha256 of the driver there.
# Both are recorded so a drift is a build failure and not a silent difference
# between the two instruments.
DRIVER_SHA="c91bc6f061649111655181ac9e8d24aa2c6025da727fe5eb1ae4a17af4f73359"

info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33m    %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

STAGES="${*:-lib link verify}"
want() { [[ " $STAGES " == *" $1 "* ]]; }

[[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 ]] || die "this rig is Apple silicon only"
[[ -x "$CC" ]] || die "no clang at $CC (set LLVM_BIN)"

got="$(shasum -a 256 "$D/signed_lookup_gem5.c" | awk '{print $1}')"
[[ "$got" == "$DRIVER_SHA" ]] || die "the staged driver is not the pinned gem5 one.
     expected $DRIVER_SHA
     got      $got
     Either restage it from gem5-DIT at the pinned commit, or update
     DRIVER_SHA here AND rerun both instruments -- they are only comparable
     while they run the same source."
info "driver sha256 matches the pinned gem5-DIT copy"

# ------------------------------------------------------------------ lib
if want lib; then
  info "libsodium arms (utils/taint_libsodium_arms.sh)"
  VARIANTS="$ARMS" ARMS_WORK="$ARMS_WORK" LLVM_BIN="$LLVM_BIN" \
    bash "$REPO/utils/taint_libsodium_arms.sh" seeds lib || die "library build failed"
fi

# ------------------------------------------------------------------ link
if want link; then
  mkdir -p "$OUT"
  # Stage the pristine driver and apply the ONE recorded patch that turns
  # HDR_CONST into a -D parameter. The committed copy is never written to, so
  # its sha256 keeps meaning what it says above; the patch is committed next to
  # it and is the whole difference between the two instruments' sources.
  STAGE="$OUT/src"
  mkdir -p "$STAGE"
  cp -f "$D/signed_lookup_gem5.c" "$STAGE/signed_lookup.c"
  cp -f "$D/pmc_ipc.h" "$D/kperf_ipc.h" "$STAGE/"
  patch -s -p0 -d "$STAGE" -i "$D/hdr_const_param.patch" \
        --input="$D/hdr_const_param.patch" 2>/dev/null \
    || patch -s -d "$STAGE" "$STAGE/signed_lookup.c" "$D/hdr_const_param.patch" \
    || die "hdr_const_param.patch did not apply to the staged driver"
  grep -q '#ifndef HDR_CONST' "$STAGE/signed_lookup.c" \
    || die "patch applied but HDR_CONST is still not overridable"
  info "staged driver patched: HDR_CONST is a -D parameter"

  info "link"
  for v in $ARMS; do
    lib="$ARMS_WORK/$v/src/libsodium/.libs/libsodium.a"
    [[ -f "$lib" ]] || { warn "skip $v -- no archive at $lib"; continue; }
    for lane in $LANES; do
      h="$(lane_hdr "$lane")"
      # -march=armv8.4-a to match the library arms and the gem5 build (FEAT_DIT
      # is armv8.4). -I"$STAGE" first so nothing else can supply kperf_ipc.h.
      "$CC" -march=armv8.4-a -O2 -g -I"$STAGE" -DHDR_CONST="$h" \
        -I"$ARMS_WORK/$v/src/libsodium/include" \
        "$STAGE/signed_lookup.c" "$lib" -o "$OUT/native_${v}_${lane}" \
        >"$OUT/.link_${v}_${lane}.log" 2>&1 \
        || { tail -20 "$OUT/.link_${v}_${lane}.log" >&2; die "link failed for $v/$lane"; }
      printf '    %-12s %-8s HDR_CONST=%s\n' "$v" "$lane" "$h"
    done
  done
  # The two lanes must differ in NOTHING but that constant. Same instruction
  # count is the check that says so, and it is cheap: a difference here means
  # the -D leaked into codegen somewhere it should not have.
  for v in $ARMS; do
    a="$OUT/native_${v}_wide"; b="$OUT/native_${v}_narrow"
    [[ -f "$a" && -f "$b" ]] || continue
    na=$("$LLVM_BIN/llvm-objdump" -d "$a" | grep -c $'\t'); nb=$("$LLVM_BIN/llvm-objdump" -d "$b" | grep -c $'\t')
    [[ "$na" == "$nb" ]] || die "the two lanes of arm '$v' differ in instruction count ($na vs $nb); they must differ only in one constant"
  done
  info "both lanes of every arm disassemble to the same instruction count"

  # What one serialising `msr DIT` costs on THIS machine. The runner divides
  # (pass - nop) cycles per request by it to get an executed switch count, so it
  # has to be measured here and not carried in from a table.
  "$CC" -march=armv8.4-a -O2 "$D/dit_switch_cost.c" -o "$OUT/dit_switch_cost" \
    >"$OUT/.link_switchcost.log" 2>&1 \
    || { tail -20 "$OUT/.link_switchcost.log" >&2; die "could not build dit_switch_cost"; }
  info "switch cost: $("$OUT/dit_switch_cost" 2>/dev/null || echo '(probe failed)')"
fi

# ------------------------------------------------------------------ verify
# Static counts per arm. These are a property of the COMPILER CONFIGURATION, so
# they must match what the gem5 rig reports for the same arm; if they do not,
# the two instruments are not running the same experiment and no amount of
# careful timing will fix it. The disassembler prints HINT #0 by its canonical
# alias `nop`, which is why the NOP arm is counted that way and not by the
# literal string (utils/taint_libsodium_arms.sh makes the same point).
if want verify; then
  info "static counts"
  # Counted in the LIBRARY ARCHIVE, not the linked binary. The driver itself
  # contains exactly one `msr DIT` -- the in-process blanket switch its
  # --blanket flag executes before the ROI -- and it is in every arm, base
  # included. Counting the binary therefore reports 1 for a perfectly NOPed
  # control and reads as "the control did not build" when nothing is wrong.
  printf '    %-12s %10s %10s %8s  %s\n' arm 'msr DIT' 'nop' 'twins' 'archive'
  for v in $ARMS; do
    lib="$ARMS_WORK/$v/src/libsodium/.libs/libsodium.a"
    [[ -f "$lib" ]] || continue
    n=$("$LLVM_BIN/llvm-objdump" -d "$lib" 2>/dev/null | grep -icE '\bmsr[[:space:]]+dit,')
    h=$("$LLVM_BIN/llvm-objdump" -d "$lib" 2>/dev/null | grep -cE '^[[:space:]]*[0-9a-f]+:.*\bnop$')
    t=$("$LLVM_BIN/llvm-nm" "$lib" 2>/dev/null | grep -c ' [TtWw] .*\.dit$')
    printf '    %-12s %10s %10s %8s  %s\n' "$v" "$n" "$h" "$t" "$(basename "$(dirname "$(dirname "$(dirname "$(dirname "$lib")")")")")"
  done
  d=$("$LLVM_BIN/llvm-objdump" -d "$OUT/native_base_wide" 2>/dev/null | grep -icE '\bmsr[[:space:]]+dit,')
  info "    driver's own msr DIT (the --blanket switch, present in every arm): $d"
  [[ "$d" == 1 ]] || warn "expected exactly 1 in the driver, found $d"
  # The two that are fatal. A zero switch count in the pass arm means the seed
  # file matched nothing and the "hardened" library is the baseline under
  # another name; a nonzero one in its twin means the layout control is not one.
  lt="$ARMS_WORK/taint/src/libsodium/.libs/libsodium.a"
  ln="$ARMS_WORK/taintnop/src/libsodium/.libs/libsodium.a"
  if [[ " $ARMS " == *" taint "* && -f "$lt" ]]; then
    n=$("$LLVM_BIN/llvm-objdump" -d "$lt" | grep -icE '\bmsr[[:space:]]+dit,')
    [[ "$n" -gt 0 ]] || die "the taint archive has no msr DIT: the seeds matched nothing"
  fi
  if [[ " $ARMS " == *" taintnop "* && -f "$ln" ]]; then
    n=$("$LLVM_BIN/llvm-objdump" -d "$ln" | grep -icE '\bmsr[[:space:]]+dit,')
    [[ "$n" -eq 0 ]] || die "the taintnop archive still has $n msr DIT: the NOP control did not build"
  fi
fi

info "done: $STAGES"
