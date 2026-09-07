#!/usr/bin/env bash
#
# Build the CIO-parity libsodium evaluation for gem5 SE mode: 6 library
# variants, 7 arms, 5 of CIO's own drivers.
#
# This is the gem5 counterpart of utils/taint_libsodium_eval.sh +
# taint_libsodium_sudo_run.sh, which target Apple silicon. It exists to run the
# ONE experiment that silicon cannot: PSTATE.DIT with switch serialisation
# removed. Experiment 09 concludes that the pass's whole cost is switch
# serialisation with no dwell term, but on an M5 that rests on cycles-per-switch
# coming out consistent across benchmarks -- a ratio derived by division. gem5
# can turn the mechanism off and test it causally.
#
# HOST NOTE: this machine is aarch64 Linux (Neoverse-N1), so nothing is
# cross-compiled and no sysroot is involved -- util/cross/taint-cross-cc is the
# macOS path and is deliberately bypassed. The N1 has no FEAT_DIT, so these
# binaries only ever execute under gem5.
#
# USAGE
#   ./build_arms.sh                # all stages
#   ./build_arms.sh lib            # library variants only
#   ./build_arms.sh link           # relink drivers against existing libraries
#
# ENV
#   LLVM=<dir>   taint LLVM build   (default: this repo's build/)
#   G5=<dir>     gem5-DIT tree      (default: this repo's gem5-DIT submodule)
#   SRC=<dir>    libsodium source   (default ~/Documents/libsodium-1.0.21)
#   CIO=<dir>    CIO checkout       (their eval_*.c live here)
#   WORK=<dir>   build root         (default ~/Documents/libsodium-cioparity)
#   JOBS=<n>     make parallelism
#   VARY_INPUT=1 patch the staged drivers so the message differs on every
#                iteration (outside the measured region). CIO's drivers feed the
#                same 100-byte message to every operation, and on a core with a
#                PC-indexed load value predictor that makes the kernel's loads
#                predictable at their ceiling, which is the whole of blanket's
#                cost on aes256-gcm decrypt. Keys and nonces already vary per
#                iteration (keygen and rand()); only the message is constant. The
#                patch is recorded in $WORK/driver_patch.diff and the byte-identity
#                check is replaced by that record.
set -uo pipefail

R="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$R/../../.." && pwd)"
LLVM="${LLVM:-$REPO/build}"
G5="${G5:-$REPO/gem5-DIT}"
SRC="${SRC:-$HOME/Documents/libsodium-1.0.21}"
WORK="${WORK:-$HOME/Documents/libsodium-cioparity}"
CIO="${CIO:?set CIO to a counter-optimization/cio checkout}"
# The pass arm's seeds. Since 2026-09-05 the compiler's defaults are the callee
# contract and the DIT twins, under which the CIO seed file protects nothing
# (its seeds sit on forwarders; see docs/results/dit-callee-contract-2026-09-04.md
# section 4). `taint` therefore takes the contract's fixpoint file and the
# owned-symbols list this script derives from the base build; `taintold` is the
# pre-flip compiler on the CIO file, for the record.
SEEDS="${SEEDS:-$G5/benchmarks/crypto/libsodium_secret_contract.txt}"
SEEDS_OLD="${SEEDS_OLD:-$G5/benchmarks/crypto/libsodium_secret.txt}"
OWNED="$WORK/owned.txt"
MARCH="${MARCH:-armv8.4-a}"
JOBS="${JOBS:-32}"
CC="$LLVM/bin/clang"

BENCHES="${BENCHES:-ed25519 chacha20_poly1305_encrypt chacha20_poly1305_decrypt aesni256gcm_encrypt aesni256gcm_decrypt}"
# arm -> library variant it links. `blanket` reuses the UNHARDENED base library
# and adds a constructor, so blanket and base are one codegen in two modes.
ARMS="${ARMS:-base blanket api taint taintnop taintold taintoldnop}"
# Library variants to build, base first (the owned list comes from it). Not
# LIBS: autoconf reads that name and would hand it to the link line.
LIB_VARIANTS="${LIB_VARIANTS:-base taintold taintoldnop taint taintnop}"

info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33m    %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

STAGES="${*:-lib link}"
want() { [[ " $STAGES " == *" $1 "* ]]; }

[[ -x "$CC" ]]     || die "no clang at $CC"
[[ -d "$SRC" ]]    || die "no libsodium source at $SRC"
[[ -f "$SEEDS" ]]  || die "no seed file at $SEEDS"
[[ -d "$CIO" ]]    || die "no CIO checkout at $CIO"
[[ -f "$G5/include/gem5/m5ops.h" ]] || die "no m5ops.h under $G5/include"
[[ -f "$G5/util/m5/build/arm64/out/libm5.a" ]] || die "no libm5.a -- build util/m5 for arm64"

mkdir -p "$WORK"
: > "$WORK/empty_seed.txt"

lib_cflags() {
  case "$1" in
    # Unhardened. Also the blanket arm's library.
    base)    echo "-O2" ;;
    # Round-trip control: the full hardening pipeline with an EMPTY seed file,
    # so zero switches are inserted. (rt - base) is the codegen cost of going
    # through the pipeline at all, which must not be charged to DIT.
    rt)      echo "-O2 -ftaint-harden=$WORK/empty_seed.txt" ;;
    # Layout control: identical placement, identical instruction count, at
    # identical addresses, every msr DIT emitted as HINT #0 so no mode switch
    # ever executes. (taint - nop) is DIT's real cost; (nop - rt) is the pure
    # code-layout cost of inserting switches. This arm is the reason the
    # serialised-minus-renamed delta is interpretable rather than just a number.
    nop)     echo "-O2 -ftaint-harden=$SEEDS -mllvm -taint-dit-nop-switches" ;;
    # Shipped defaults (2026-09-05): region placement, callee contract, DIT twins,
    # the contract seeds and the owned list.
    # TAINT_EXTRA: extra -mllvm flags for the pass arm and its NOP twin (e.g.
    # the twin-narrowing knobs), so an A/B differs from the default by them alone.
    taint)      echo "-O2 -ftaint-harden=$SEEDS -mllvm -taint-owned-symbols=$OWNED ${TAINT_EXTRA:-}" ;;
    taintnop)   echo "-O2 -ftaint-harden=$SEEDS -mllvm -taint-owned-symbols=$OWNED ${TAINT_EXTRA:-} -mllvm -taint-dit-nop-switches" ;;
    # The shipped defaults MINUS the twins, and its own NOP layout control.
    # These exist to SPLIT the term the rig used to call "layout": (rt - base)
    # is the codegen `-ftaint-harden` costs before a single switch is placed
    # (chiefly the TU-wide tail-call disable, which is a hardening decision and
    # not a lottery); (notwinnop - rt) is the pure address shift of the switch
    # slots; (taintnop - notwinnop) is what duplicating the reached set into
    # `.dit` twins costs the front end. Without them all three land in one
    # number and the number looks like an unattributable lottery.
    notwin)      echo "-O2 -ftaint-harden=$SEEDS -mllvm -taint-owned-symbols=$OWNED ${TAINT_EXTRA:-} -mllvm -taint-dit-clone-seeded=0" ;;
    notwinnop)   echo "-O2 -ftaint-harden=$SEEDS -mllvm -taint-owned-symbols=$OWNED ${TAINT_EXTRA:-} -mllvm -taint-dit-clone-seeded=0 -mllvm -taint-dit-nop-switches" ;;
    # The pre-2026-09-05 compiler: inherit contract, no twins, the CIO seeds.
    taintold)    echo "-O2 -ftaint-harden=$SEEDS_OLD -mllvm -taint-dit-contract=inherit -mllvm -taint-dit-clone-seeded=0" ;;
    taintoldnop) echo "-O2 -ftaint-harden=$SEEDS_OLD -mllvm -taint-dit-contract=inherit -mllvm -taint-dit-clone-seeded=0 -mllvm -taint-dit-nop-switches" ;;
    taintfn) echo "-O2 -ftaint-harden=$SEEDS -mllvm -taint-dit-placement=function" ;;
    # Pre-2026-08-24 defaults. The arm that produced experiment 09's +153%/+166%
    # column; a historical policy, not a default.
    fine)    echo "-O2 -ftaint-harden=$SEEDS -mllvm -taint-dit-switch-cyc=0 -mllvm -taint-dit-loop-hoist=0" ;;
    *) die "unknown variant: $1" ;;
  esac
}

# ------------------------------------------------------------------ lib
if want lib; then
for v in $LIB_VARIANTS; do
  W="$WORK/$v"
  if [[ -f "$W/src/libsodium/.libs/libsodium.a" ]]; then
    info "libsodium '$v' already built -- skipping"
    [[ "$v" == base && ! -f "$OWNED" ]] && "$LLVM/bin/llvm-nm" --defined-only --no-demangle "$W/src/libsodium/.libs/libsodium.a" 2>/dev/null \
      | awk 'NF == 3 && $2 ~ /^[tTwW]$/ { print $3 }' | sed 's/^\.L//' | sort -u > "$OWNED"
    continue
  fi
  info "libsodium variant '$v'"
  rm -rf "$W"; mkdir -p "$W"
  ( cd "$SRC" && tar cf - --exclude=.git --exclude='*.o' --exclude='*.lo' \
      --exclude='*.a' --exclude='*.la' --exclude=.libs . ) | ( cd "$W" && tar xf - ) \
    || die "could not copy source for $v"

  # CIO seeds three symbols that only exist after THEIR rename patch, all
  # statics in crypto_stream/chacha20/ref/chacha20_ref.c. Without this the seed
  # file silently under-seeds: unmatched names are ignored without warning.
  # \b matters -- stream_ref must not match inside stream_ref_xor_ic.
  f="$W/src/libsodium/crypto_stream/chacha20/ref/chacha20_ref.c"
  [[ -f "$f" ]] || die "not found: $f"
  perl -pi -e 's/\bstream_ref\b/stream_ref_ref/g;
               s/\bstream_ref_xor_ic\b/stream_ref_xor_ic_ref/g;
               s/\bchacha20_encrypt_bytes\b/chacha20_encrypt_bytes_ref/g' "$f"
  grep -q 'chacha20_encrypt_bytes_ref' "$f" || die "rename patch did not apply for $v"

  # --disable-asm is REQUIRED, not a tuning choice: hand-written .S never goes
  # through the pass, so anything in it would be invisible to the analysis.
  # Experiment 09 builds the same way. --host forces autoconf's cross path so
  # configure never RUNS a test binary -- these are built for armv8.4-a and this
  # host is armv8.2-a, so a run test could die on an unsupported instruction.
  ( cd "$W" && CC="$CC" \
      ./configure --host=aarch64-linux-gnu \
        --disable-shared --enable-static --disable-asm --disable-pie \
        CFLAGS="-march=$MARCH $(lib_cflags "$v")" > configure.log 2>&1 ) \
    || { tail -25 "$W/configure.log" >&2; die "configure failed for $v"; }
  ( cd "$W" && make -j"$JOBS" > build.log 2>&1 ) \
    || { tail -30 "$W/build.log" >&2; die "build failed for $v"; }

  lib="$W/src/libsodium/.libs/libsodium.a"
  [[ -f "$lib" ]] || die "no archive produced for $v"
  n=$("$LLVM/bin/llvm-objdump" -d "$lib" 2>/dev/null | grep -icE '\bmsr\b[[:space:]]+dit,')
  h=$("$LLVM/bin/llvm-objdump" -d "$lib" 2>/dev/null | grep -cE '\bhint\b[[:space:]]+#0')
  t=$("$LLVM/bin/llvm-nm" "$lib" 2>/dev/null | grep -c ' [TtWw] .*\.dit$')
  info "    $(du -h "$lib" | cut -f1)   msr DIT: $n   hint #0: $h   twins: $t"
  # The functions this build defines, from the unhardened objects: what the
  # twins may be named across TUs and what the obligation report may propose.
  if [[ "$v" == base ]]; then
    "$LLVM/bin/llvm-nm" --defined-only --no-demangle "$lib" 2>/dev/null \
      | awk 'NF == 3 && $2 ~ /^[tTwW]$/ { print $3 }' | sed 's/^\.L//' | sort -u > "$OWNED"
    info "    owned symbols: $(wc -l < "$OWNED")"
  fi
done
fi

# ------------------------------------------------------------------ link
if want link; then
# CIO's drivers use a QUOTED include ("eval_util.h"), which searches the
# directory of the including file BEFORE any -I path. So their x86 header always
# wins in their own tree and -I cannot override it. Stage byte-identical copies
# of the drivers next to our header instead, and record sha256 for each so
# "unmodified" is a checkable claim rather than an assertion. CIO's checkout is
# never written to.
info "stage CIO drivers (byte-identical copies; only eval_util.h is ours)"
STAGE="$WORK/src"
mkdir -p "$STAGE" "$WORK/bin"
cp -f "$R/eval_util.h" "$STAGE/eval_util.h"
: > "$WORK/driver_sha256.txt"
for b in $BENCHES; do
  [[ -f "$CIO/eval_$b.c" ]] || continue
  cp -f "$CIO/eval_$b.c" "$STAGE/eval_$b.c"
  ( cd "$CIO" && sha256sum "eval_$b.c" ) >> "$WORK/driver_sha256.txt"
done
( cd "$STAGE" && sha256sum -c "$WORK/driver_sha256.txt" >/dev/null 2>&1 ) \
  && info "    drivers byte-identical to the CIO checkout" \
  || die "staged driver differs from CIO source"
if [[ "${VARY_INPUT:-0}" == 1 ]]; then
  # Rewrite the message before each iteration's setup, so every operation
  # encrypts/signs different bytes. A fixed mix of the previous byte, the
  # iteration and the index: deterministic, so every arm sees the same
  # sequence. Argon2id's input is its password and a random salt; untouched.
  : > "$WORK/driver_patch.diff"
  for b in $BENCHES; do
    f="$STAGE/eval_$b.c"; [[ -f "$f" ]] || continue
    [[ "$b" == argon2id ]] && continue
    cp "$f" "$f.orig"
    perl -pi -e 's|^(\s*for \(int cur_iter = 0; cur_iter < num_iter \+ num_warmup; \+\+cur_iter\) \{)\s*$|$1\n    /* VARY_INPUT: a different message every iteration, outside the ROI. */\n    for (unsigned long long vi_ = 0; vi_ < msg_sz; ++vi_)\n      msg[vi_] = (unsigned char)(msg[vi_] * 31u + (unsigned)cur_iter * 17u + (unsigned)vi_ + 1u);\n|' "$f"
    grep -q 'VARY_INPUT' "$f" || die "VARY_INPUT patch did not apply to eval_$b.c (loop header not found)"
    diff -u "$f.orig" "$f" >> "$WORK/driver_patch.diff" || true
    rm -f "$f.orig"
  done
  warn "VARY_INPUT=1: drivers patched, see $WORK/driver_patch.diff (NOT byte-identical to CIO)"
fi

info "link"
for b in $BENCHES; do
  src="$STAGE/eval_$b.c"
  [[ -f "$src" ]] || { warn "skip $b -- no $src"; continue; }
  for arm in $ARMS; do
    wraps=""; src2=""
    case "$arm" in
      blanket) v=base; extra="-DBLANKET_DIT" ;;
      # Hand placement at the public API: the base library, the entry points
      # CIO's driver calls wrapped by the linker so each is bracketed by one
      # enable and one clear (api_bracket.c). Two mode writes per call.
      # api: Apple's prologue/epilogue (mrs token, msr DIT #1, isb sy in place
      # of sb, conditional restore). apidsb: Apple's no-SB fallback barrier
      # (dsb nsh; isb sy). apibare: the pre-2026-09-05 bracket, no token read
      # and no barrier, two unconditional writes.
      # apiisb: the isb without the token read (msr DIT #1; isb; ...; msr DIT #0),
      # which separates the barrier's cost from gem5's mrs DIT drain.
      # apiisbnop: apiisb with HINT #0 in place of the isb, the layout control.
      api|apidsb|apibare|apiisb|apiisbnop)
               v=base; src2="$R/api_bracket.c"
               case "$arm" in
                 apidsb)    bar="-DAPI_BARRIER_DSBISB" ;;
                 apibare)   bar="-DAPI_BARRIER_NONE -DAPI_NO_MRS" ;;
                 apiisb)    bar="-DAPI_NO_MRS" ;;
                 apiisbnop) bar="-DAPI_NO_MRS -DAPI_BARRIER_NOP" ;;
                 *)       bar="" ;;
               esac
               case "$b" in
                 ed25519)        extra="-DAPI_SIGN";   syms="crypto_sign_keypair crypto_sign crypto_sign_open" ;;
                 chacha20*)      extra="-DAPI_CHACHA"; syms="crypto_aead_chacha20poly1305_ietf_keygen crypto_aead_chacha20poly1305_ietf_encrypt crypto_aead_chacha20poly1305_ietf_decrypt" ;;
                 aesni256gcm*)   extra="-DAPI_AES";    syms="crypto_aead_aes256gcm_keygen crypto_aead_aes256gcm_encrypt crypto_aead_aes256gcm_decrypt" ;;
                 argon2id)       extra="-DAPI_PWHASH"; syms="crypto_pwhash" ;;
                 *) die "no API bracket table for $b" ;;
               esac
               extra="$extra $bar"
               for sy in $syms; do wraps="$wraps -Wl,--wrap=$sy"; done ;;
      *)       v="$arm"; extra="" ;;
    esac
    lib="$WORK/$v/src/libsodium/.libs/libsodium.a"
    [[ -f "$lib" ]] || { warn "skip $b/$arm -- no library for '$v'"; continue; }
    # NO_DYN_HIT_COUNTS: their opcode instrumentation is x86 and not our
    # mitigation. The drivers are compiled at -O2, not CIO's -O0: at -O0 the
    # volatile timer stores and unregistered argument setup land INSIDE the
    # measured region and compress every ratio. Experiment 09 measured both and
    # recorded -O0 as worth at most +0.8pp.
    "$CC" -march="$MARCH" -O2 -std=gnu18 -static -fomit-frame-pointer \
        -DNO_DYN_HIT_COUNTS $extra \
        -I"$R" -I"$G5/include" -I"$WORK/$v/src/libsodium/include" \
        -o "$WORK/bin/eval_${b}.${arm}" "$src" "$R/blanket_ctor.c" $src2 \
        "$lib" $wraps -L"$G5/util/m5/build/arm64/out" -lm5 -lm \
      >"$WORK/bin/.link_${b}_${arm}.log" 2>&1 \
      || { warn "link failed $b/$arm"; tail -6 "$WORK/bin/.link_${b}_${arm}.log" >&2; continue; }
  done
  printf '    %-34s %s\n' "$b" "$(ls "$WORK/bin/eval_${b}."* 2>/dev/null | wc -l) arms"
done
ls -la "$WORK/bin"/eval_* 2>/dev/null | awk '{printf "  %10s  %s\n", $5, $9}' | head -40
fi

info "done: $STAGES"
