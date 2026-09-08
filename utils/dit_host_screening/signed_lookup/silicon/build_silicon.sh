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
#   bracket    THE APPLE BRACKET: the unhardened library, with the AEAD entry
#              points the driver calls wrapped in Apple's own prologue and
#              epilogue -- read the previous DIT state, `msr DIT, #1`, a
#              speculation barrier, the call, and clear only if it was clear.
#              This is what AWS-LC ships (armv8_get_dit / armv8_set_dit /
#              armv8_restore_dit in crypto/fipsmodule/cpucap/cpu_aarch64.c,
#              experiment 14's `ditsb` variant) and what Apple's
#              "Writing ARM64 code for Apple platforms" tells a library author
#              to do. Two mode writes and one barrier per CALL, against the
#              pass's ~40 writes per request. Measured on this part, one whole
#              bracket is 106 cycles: 67 for the two writes, 11 for the token
#              read, 28 for `sb` (dit_switch_cost.c and the README).
#   bracketnop the bracket's INSTRUCTION-MATCHED twin, experiment 14's `ditnop`:
#              the token read becomes `mov x, xzr`, both writes and the barrier
#              become `nop`, and because the read now yields 0 the restore takes
#              the same branch the real bracket takes. Same instruction count at
#              the same addresses, no mode ever changes. (bracket - bracketnop)
#              is the bracket's real cost and the rest is layout. Experiment 09
#              had to borrow a barrier arm for this and it cost it a wrong
#              column; do not run the bracket without it.
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
#   SECRET_OP=<op>    chacha (default) | aes -- WHICH AEAD is the secret lane
#   SECRET_MLEN=<n>   secret bytes per request (default 100)
set -uo pipefail

D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$D/../../../.." && pwd)"
LLVM_BIN="${LLVM_BIN:-$REPO/build/bin}"
ARMS_WORK="${ARMS_WORK:-$HOME/Documents/libsodium-arms-m4}"
OUT="${OUT:-$D/bin}"
# What gets LINKED. `bracket` and `bracketnop` are not library variants -- they
# link the unhardened `base` archive with a wrapper object, so there is nothing
# extra to compile in libsodium for them.
ARMS="${ARMS:-base taint taintnop bracket bracketnop bracketnobar bracketdsb bracketdsbnop}"
# WHICH AEAD IS THE SECRET LANE, and how many bytes of it per request.
#
# `chacha` at 100 bytes is the driver's own lane and what every M4 sweep in
# paper_experiments/02 ran. `aes` selects AES-256-GCM, the lane the GEM5 panel
# runs -- gem5 cannot resolve a chacha bracket at any message size (chacha is a
# serial ARX chain that does not fill the reorder window until the message is
# ~4 KB, by which point the request is 46,000 cycles and a 300-cycle switch is
# 0.6% of it). So `SECRET_OP=aes SECRET_MLEN=64` is how this rig builds the SAME
# microbenchmark the simulator half runs.
#
# The op is a preprocessor parameter of the staged driver, added by
# secret_op_param.patch; the vendored source is still never edited. One knob and
# not three because it must change three things together: the driver AEAD calls,
# api_bracket.c's wrapped entry points, and the preprocessor renames that
# interpose them (Apple ld64 has no --wrap). AES-256-GCM needs hardware AES; the
# driver checks crypto_aead_aes256gcm_is_available() and dies if it is absent.
SECRET_OP="${SECRET_OP:-chacha}"
SECRET_MLEN="${SECRET_MLEN:-100}"
case "$SECRET_OP" in
  chacha)
    API_OP_FLAG="-DAPI_CHACHA"
    OP_DEFS=(-DAEAD_MLEN="$SECRET_MLEN")
    OP_RENAME=(
      "-Dcrypto_aead_chacha20poly1305_ietf_encrypt=expedite_api_crypto_aead_chacha20poly1305_ietf_encrypt"
      "-Dcrypto_aead_chacha20poly1305_ietf_keygen=expedite_api_crypto_aead_chacha20poly1305_ietf_keygen"
    ) ;;
  aes)
    API_OP_FLAG="-DAPI_AES"
    OP_DEFS=(-DSECRET_AES -DAEAD_MLEN="$SECRET_MLEN")
    OP_RENAME=(
      "-Dcrypto_aead_aes256gcm_encrypt=expedite_api_crypto_aead_aes256gcm_encrypt"
      "-Dcrypto_aead_aes256gcm_keygen=expedite_api_crypto_aead_aes256gcm_keygen"
    ) ;;
  *) echo "build_silicon: unknown SECRET_OP: $SECRET_OP (chacha | aes)" >&2; exit 1 ;;
esac
# What taint_libsodium_arms.sh has to BUILD.
LIB_VARIANTS="${LIB_VARIANTS:-base taint taintnop}"

# arm -> the libsodium archive it links.
arm_lib() {
  case "$1" in
    bracket|bracketnop|bracketnobar|bracketdsb|bracketdsbnop) echo base ;;
    *) echo "$1" ;;
  esac
}
# arm -> the -D set for api_bracket.c, empty for the arms that do not use it.
#
# api_bracket.c is experiment 09's file, unmodified and shared: this rig is the
# third consumer, and keeping one copy is what makes "the same bracket on both
# instruments" a fact rather than a claim. API_BARRIER_SB selects Apple's real
# `sb` by its raw encoding (0xd50330ff), which needs no -march and which THIS
# PART EXECUTES (checked at build time below); gem5 has no FEAT_SB, which is why
# that rig defaults to `isb sy` instead and why the two barriers are separate
# knobs rather than one.
arm_bracket_flags() {
  case "$1" in
    bracket)    echo "$API_OP_FLAG -DAPI_MACRO_RENAME -DAPI_BARRIER_SB" ;;
    bracketnop) echo "$API_OP_FLAG -DAPI_MACRO_RENAME -DAPI_BARRIER_SB -DAPI_NOP" ;;
    # Apple's sequence MINUS the speculation barrier. Not a configuration
    # anyone should ship -- without it the mode change is not architecturally
    # guaranteed to be in effect for what follows, which is the whole reason
    # Apple's text asks for it. It is here to split the bracket's bill:
    # (bracket - bracketnobar) is what `sb` costs and the rest is the token
    # read and the two writes. In situ that split is not what a tight loop
    # predicts, which is the point.
    bracketnobar) echo "$API_OP_FLAG -DAPI_MACRO_RENAME -DAPI_BARRIER_NONE" ;;
    # Apple's documented FALLBACK for a part without FEAT_SB: `dsb nsh; isb sy`
    # in place of `sb`. Not what this M4 needs -- it has FEAT_SB and Apple's own
    # libsystem_platform uses `sb` on it -- but it is the barrier gem5 can
    # actually model, since it implements both dsb (IsSerializeAfter) and isb
    # (IsSquashAfter) and no `sb` at all. If this arm lands near the `sb` one,
    # the gem5 bracket column can be made comparable by building it
    # -DAPI_BARRIER_DSBISB instead of leaving it on the isb-only default.
    bracketdsb) echo "$API_OP_FLAG -DAPI_MACRO_RENAME -DAPI_BARRIER_DSBISB" ;;
    # bracketdsb's OWN twin. It cannot share bracketnop: `dsb nsh; isb sy` is
    # two instructions where `sb` is one, so bracketnop is an instruction short
    # of it. api_bracket.c's API_NOP now emits as many hints as the selected
    # barrier has instructions, so this twin matches at 18 where bracketnop
    # matches bracket at 17.
    bracketdsbnop) echo "$API_OP_FLAG -DAPI_MACRO_RENAME -DAPI_BARRIER_DSBISB -DAPI_NOP" ;;
    *) echo "" ;;
  esac
}
# The driver's calls into the wrapped entry points, renamed at the PREPROCESSOR
# so the driver source still does not change. Apple's ld64 has no --wrap, which
# is how the gem5 rig interposes; api_bracket.c's API_MACRO_RENAME path exists
# for exactly this. Only the two the driver actually calls are renamed --
# `_decrypt`'s wrapper is compiled and never reached, in the bracket arm and in
# its twin alike, so it cannot move one relative to the other.
BRACKET_RENAME=("${OP_RENAME[@]}")
BRACKET_SRC="$REPO/utils/dit_host_screening/cioparity/api_bracket.c"
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
  VARIANTS="$LIB_VARIANTS" ARMS_WORK="$ARMS_WORK" LLVM_BIN="$LLVM_BIN" \
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
  # ...and the second recorded patch, which makes the secret lane's OP and SIZE
  # -D parameters. Applied unconditionally, including for SECRET_OP=chacha: its
  # defaults are the driver's own (chacha20-poly1305, 100 bytes), so the staged
  # source is semantically unchanged and one staging path serves both ops. The
  # -D on the compile line is the only thing that selects.
  patch -s -p0 -d "$STAGE" -i "$D/secret_op_param.patch" \
        --input="$D/secret_op_param.patch" 2>/dev/null \
    || patch -s -d "$STAGE" "$STAGE/signed_lookup.c" "$D/secret_op_param.patch" \
    || die "secret_op_param.patch did not apply to the staged driver"
  grep -q '#ifndef AEAD_MLEN' "$STAGE/signed_lookup.c" \
    || die "patch applied but AEAD_MLEN is still not overridable"
  info "staged driver patched: HDR_CONST, AEAD_MLEN and the secret op are -D parameters"
  info "secret lane: $SECRET_OP, $SECRET_MLEN bytes per request"

  # Does this part execute Apple's `sb`? The bracket arm is only Apple's
  # bracket if it does; on a part without FEAT_SB Apple's own fallback is
  # `dsb nsh; isb sy` and the arm would have to say so.
  cat > "$STAGE/sbprobe.c" <<'EOF'
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
static sigjmp_buf jb;
static void ill(int s) { (void) s; siglongjmp(jb, 1); }
int main(void) {
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = ill;
    sigaction(SIGILL, &sa, &old);
    int ok = 0;
    if (sigsetjmp(jb, 1) == 0) { __asm__ volatile(".inst 0xd50330ff" ::: "memory"); ok = 1; }
    sigaction(SIGILL, &old, NULL);
    puts(ok ? "yes" : "no");
    return ok ? 0 : 1;
}
EOF
  "$CC" -O2 "$STAGE/sbprobe.c" -o "$STAGE/sbprobe" >/dev/null 2>&1 \
    && "$STAGE/sbprobe" >/dev/null 2>&1 \
    && info "FEAT_SB: this part executes sb, so the bracket is Apple's own sequence" \
    || warn "this part does NOT execute sb -- the bracket arm is not Apple's sequence here; use -DAPI_BARRIER_DSBISB"

  info "link"
  for v in $ARMS; do
    lib="$ARMS_WORK/$(arm_lib "$v")/src/libsodium/.libs/libsodium.a"
    [[ -f "$lib" ]] || { warn "skip $v -- no archive at $lib"; continue; }
    bf="$(arm_bracket_flags "$v")"
    extra=(); obj=()
    if [[ -n "$bf" ]]; then
      # api_bracket.c is compiled WITHOUT the renames. With them, API_REAL(f)
      # would expand to the wrapper's own name and every bracketed call would
      # recurse into itself.
      "$CC" -march=armv8.4-a -O2 -c "$BRACKET_SRC" $bf -o "$OUT/.brk_$v.o" \
        >"$OUT/.link_$v.log" 2>&1 \
        || { tail -20 "$OUT/.link_$v.log" >&2; die "could not compile api_bracket.c for $v"; }
      obj=("$OUT/.brk_$v.o"); extra=("${BRACKET_RENAME[@]}")
    fi
    for lane in $LANES; do
      h="$(lane_hdr "$lane")"
      # -march=armv8.4-a to match the library arms and the gem5 build (FEAT_DIT
      # is armv8.4). -I"$STAGE" first so nothing else can supply kperf_ipc.h.
      "$CC" -march=armv8.4-a -O2 -g -I"$STAGE" -DHDR_CONST="$h" \
        "${OP_DEFS[@]}" ${extra[@]+"${extra[@]}"} \
        -I"$ARMS_WORK/$(arm_lib "$v")/src/libsodium/include" \
        "$STAGE/signed_lookup.c" ${obj[@]+"${obj[@]}"} "$lib" -o "$OUT/native_${v}_${lane}" \
        >>"$OUT/.link_${v}_${lane}.log" 2>&1 \
        || { tail -20 "$OUT/.link_${v}_${lane}.log" >&2; die "link failed for $v/$lane"; }
      printf '    %-12s %-8s HDR_CONST=%s%s\n' "$v" "$lane" "$h" \
        "$([[ -n "$bf" ]] && echo "  [api_bracket.c $bf]")"
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
  # The bracket and its twin, in the wrapper only. The real one must carry
  # Apple's four instructions (mrs DIT, msr #1, sb, msr #0) and the twin must
  # carry none of them while disassembling to the same length.
  W=_expedite_api_crypto_aead_chacha20poly1305_ietf_encrypt
  if [[ -f "$OUT/native_bracket_narrow" ]]; then
    d=$("$LLVM_BIN/llvm-objdump" -d --disassemble-symbols=$W "$OUT/native_bracket_narrow" 2>/dev/null)
    for want in 'mrs[[:space:]]+x[0-9]+, DIT' 'msr[[:space:]]+DIT, #0x1' '\bsb\b' 'msr[[:space:]]+DIT, #0x0'; do
      grep -qE "$want" <<< "$d" || die "the bracket wrapper is missing '$want' -- it is not Apple's sequence"
    done
    info "    bracket wrapper: mrs DIT / msr DIT,#1 / sb / call / tbnz / msr DIT,#0"
  fi
  if [[ -f "$OUT/native_bracketnop_narrow" ]]; then
    d=$("$LLVM_BIN/llvm-objdump" -d --disassemble-symbols=$W "$OUT/native_bracketnop_narrow" 2>/dev/null)
    grep -qE '(mrs|msr)[[:space:]]+(x[0-9]+, )?DIT' <<< "$d" \
      && die "the bracket NOP twin still touches DIT: it is not a layout control"
    a=$("$LLVM_BIN/llvm-objdump" -d --disassemble-symbols=$W "$OUT/native_bracket_narrow" 2>/dev/null | grep -c $'\t')
    b=$(grep -c $'\t' <<< "$d")
    [[ "$a" == "$b" ]] || die "bracket wrapper is $a instructions and its twin $b -- not instruction-matched"
    info "    bracket NOP twin: $b instructions, same as the bracket, none of them DIT"
  fi
fi

info "done: $STAGES"
