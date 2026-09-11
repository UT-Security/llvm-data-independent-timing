#!/usr/bin/env bash
# Experiment 14 on gem5: AWS-LC v5.8.0, the bracket Amazon ships, in four STATIC builds
# of `bssl speed` carrying m5 ROI markers instead of Apple PMC reads.
#
#   rel        -DENABLE_DATA_INDEPENDENT_TIMING=OFF     the unhardened baseline
#   ditsb      ...=ON, as shipped, plus `sb` after the enable -- THE SILICON BINARY
#   ditisb     ...=ON, as shipped, plus `isb sy` after the enable  (superseded, see below)
#
# ONE hardened build for BOTH switch models. Apple's design needs the barrier; the renamed
# switch does not, and gem5 drops its ORDERING at rename (ditFuseBarrierAfterMsr, implied
# by --expedite) while still fetching, decoding and retiring it. So both models run the same
# binary and the barrier's cost is measured with instruction count and layout held constant,
# rather than across a relink between a `dit` build and a `ditisb` one.
#
# `sb` IS the barrier now, and that is the point of ditsb: gem5 implements FEAT_SB (Sb64 in
# arch/arm/isa/insts/misc64.isa, ID_AA64ISAR1_EL1.SB advertised), so the gem5 rig runs the
# SAME BINARY the silicon rig runs instead of substituting `isb sy` for it. `isb` was only
# ever a stand-in for a simulator without FEAT_SB and it is not the same barrier: measured
# on tests/test-progs/feat_sb, `sb` costs 5 cycles per bracket entry against `isb`'s 12 and
# `dsb sy; isb`'s 25, and the reason is structural -- ISB is a context synchronization event
# and squashes, SB only bars speculation. ditisb is kept buildable so the earlier gem5
# numbers stay reproducible; both barriers fuse under --expedite, so one gem5 build measures
# either. (patch_bracket_variant.py still knows `ditnop`; nothing builds it by default --
# the silicon experiment has no such arm and the two tables must compare.)
#
# Static, because every gem5 rig in this tree is: gem5 SE has no loader to preload into,
# which is also why arm C is an env var read by a constructor linked into every build
# (blanket_ctor.c) rather than an inserted library.
#
# DISABLE_CPU_JITTER_ENTROPY, the one deviation from the vendor's default configuration.
# AWS-LC seeds its DRBG from two independent sources, one of them CPU jitter entropy, which
# times itself against the ARM virtual counter. gem5 SE does not service `cntvct_el0`
# ("attempts to access a system timer which is inaccessible within SE mode"), so jitter
# entropy fails its health test and every benchmark that draws from the DRBG produces NO
# ROW AT ALL: measured on `-filter P-256`, 34 cntvct warnings and zero JSON rows, against
# zero warnings and a clean run on the AEAD filter, which needs no randomness. That costs
# two of the ten paper rows (ECDSA P-256 sign, RNG). With the switch, seeding falls back to
# getrandom, which gem5 SE does implement. It changes the entropy source only, not the
# bracket, the crypto, or any row's instruction stream outside DRBG seeding -- but the RNG
# row measures the DRBG, so on that row it is a change of workload and must be said so.
#
#   build_awslc_gem5.sh fetch | prep | build | all
# CLANG, NOT THE PLATFORM cc. The silicon rig builds with whatever `cc` is, which on macOS is
# clang, and clang inlines armv8_set_dit/armv8_restore_dit into every bracketed entry point:
# 277 sites on the M4, 281 here. GCC 15 on Linux DECLINES to inline them and emits 189
# `bl armv8_set_dit` / 197 `bl armv8_restore_dit` call sites instead. That is a different
# workload -- a call and a capability test per entry on top of the mode writes -- and it would
# not be comparable to the M4 table. The site-count gate below is what catches it.
#
# Env: W (default ~/Documents/dit-awslc-gem5), G5 (default <repo>/gem5-DIT), CC_BIN/CXX_BIN, JOBS
set -uo pipefail
RIG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$RIG/../../.." && pwd)"
G5="${G5:-$REPO/gem5-DIT}"
W="${W:-$HOME/Documents/dit-awslc-gem5}"
V=5.8.0; TAG="v$V"; SRC="$W/src/aws-lc-$V"; URL="https://github.com/aws/aws-lc/archive/refs/tags/$TAG.tar.gz"
CC_BIN="${CC_BIN:-clang}"; CXX_BIN="${CXX_BIN:-clang++}"; JOBS="${JOBS:-24}"
VARIANTS="${VARIANTS:-rel ditsb}"
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
OD="$(command -v objdump)"
# -i throughout: objdump prints the operand as DIT on some builds and dit on others, and
# the case-sensitive form silently reports zero on a correctly bracketed binary (CLAUDE.md).
count() { "$OD" -d "$1" | grep -ciE "$2"; }

[[ -f "$G5/include/gem5/m5ops.h" ]] || die "no m5ops.h under $G5/include"
[[ -f "$G5/util/m5/build/arm64/out/libm5.a" ]] || die "no libm5.a: (cd $G5/util/m5 && scons build/arm64/out/libm5.a)"
[[ "$(uname -m)" == aarch64 ]] || die "build this on an aarch64 host: the binaries are native AArch64 for gem5 SE"

do_fetch() {
  mkdir -p "$W/src"
  [[ -d "$SRC" ]] && { info "source present: $SRC"; return; }
  info "fetching $URL"; curl -fsSL -o "$W/src/aws-lc-$TAG.tar.gz" "$URL" || die "download failed"
  tar xzf "$W/src/aws-lc-$TAG.tar.gz" -C "$W/src" || die "extract failed"
  sha256sum "$W/src/aws-lc-$TAG.tar.gz" | cut -c1-16 | sed 's/^/    tarball sha256 /'
}
do_prep() {
  for v in $VARIANTS; do
    info "tree-$v"; rm -rf "$W/tree-$v"; cp -R "$SRC" "$W/tree-$v"
    python3 "$RIG/patch_speed_m5.py" "$W/tree-$v" || die "speed patch failed ($v)"
    case $v in rel|dit) ;; *) python3 "$RIG/patch_bracket_variant.py" "$W/tree-$v" "$v" || die "variant patch failed ($v)" ;; esac
  done
  info "blanket_ctor.o"; "$CC_BIN" -O2 -c -o "$W/blanket_ctor.o" "$RIG/blanket_ctor.c" || die blanket_ctor
}
do_build() {
  : > "$W/switch_counts.txt"
  local pids=() vs=()
  for v in $VARIANTS; do
    flag=ON; [[ $v == rel ]] && flag=OFF
    info "build-$v (ENABLE_DATA_INDEPENDENT_TIMING=$flag)"
    (
      cmake -G Ninja -S "$W/tree-$v" -B "$W/build-$v" -DCMAKE_BUILD_TYPE=Release \
          -DBUILD_TESTING=OFF -DBUILD_TOOL=ON -DDISABLE_GO=ON \
          -DDISABLE_CPU_JITTER_ENTROPY=ON \
          -DENABLE_DATA_INDEPENDENT_TIMING=$flag \
          -DCMAKE_C_COMPILER="$CC_BIN" -DCMAKE_CXX_COMPILER="$CXX_BIN" \
          -DCMAKE_C_FLAGS="-I$G5/include" -DCMAKE_CXX_FLAGS="-I$G5/include" \
          -DCMAKE_EXE_LINKER_FLAGS="-static" \
          -DCMAKE_CXX_STANDARD_LIBRARIES="$W/blanket_ctor.o -L$G5/util/m5/build/arm64/out -lm5" \
          > "$W/cmake-$v.log" 2>&1 \
        || { tail -20 "$W/cmake-$v.log"; exit 1; }
      ninja -C "$W/build-$v" -j"$JOBS" bssl > "$W/ninja-$v.log" 2>&1 \
        || { tail -25 "$W/ninja-$v.log"; exit 1; }
    ) & pids+=($!); vs+=("$v")
  done
  local rc=0
  for i in "${!pids[@]}"; do wait "${pids[$i]}" || { warn_v="${vs[$i]}"; printf '\033[31mbuild failed: %s\033[0m\n' "$warn_v" >&2; rc=1; }; done
  [[ $rc -eq 0 ]] || die "one or more builds failed"
  for v in $VARIANTS; do
    b="$W/build-$v/tool/bssl"; [[ -x "$b" ]] || die "no bssl for $v"
    # The FIPS module is one translation unit (bcm.c includes every fipsmodule .c), so with
    # the option ON the compiler inlines armv8_set_dit/armv8_restore_dit into every bracketed
    # entry point: one DIT read and two DIT writes per site. With it OFF only the two
    # out-of-line copies remain (the speed tool's -dit path calls them). The gate counts sites.
    echo "$v mrs_dit=$(count "$b" 'mrs[[:space:]]+x[0-9]+,[[:space:]]*(dit|s3_3_c4_c2_5)') msr_dit=$(count "$b" 'msr[[:space:]]+dit,') isb=$(count "$b" 'isb([[:space:]]|$)') sb=$(count "$b" 'd50330ff') m5=$(count "$b" '\.inst.*0x040f0000|m5_reset') static=$(file "$b" | grep -c 'statically linked')" | tee -a "$W/switch_counts.txt"
  done
  # gates: what must hold, not numbers to eyeball
  for v in $VARIANTS; do
    file "$W/build-$v/tool/bssl" | grep -q 'statically linked' || die "$v is not static: gem5 SE has no loader here"
    grep -q 'bm_m5_calls' "$W/tree-$v/tool/speed.cc" || die "$v: the m5 speed patch is not in the tree"
  done
  [[ " $VARIANTS " == *" rel "*   ]] && { [[ $(count "$W/build-rel/tool/bssl" 'mrs[[:space:]]+x[0-9]+,[[:space:]]*(dit|s3_3_c4_c2_5)') -le 2 ]] || die "rel brackets its entry points (the option is not off)"; }
  [[ " $VARIANTS " == *" ditisb "* ]] && { [[ $(count "$W/build-ditisb/tool/bssl" 'mrs[[:space:]]+x[0-9]+,[[:space:]]*(dit|s3_3_c4_c2_5)') -ge 50 ]] || die "ditisb carries too few bracket sites"; }
  [[ " $VARIANTS " == *" ditisb "* ]] && { [[ $(count "$W/build-ditisb/tool/bssl" 'isb([[:space:]]|$)') -ge 50 ]] || die "ditisb carries too few isb"; }
  [[ " $VARIANTS " == *" ditsb "*  ]] && { [[ $(count "$W/build-ditsb/tool/bssl" 'mrs[[:space:]]+x[0-9]+,[[:space:]]*(dit|s3_3_c4_c2_5)') -ge 50 ]] || die "ditsb carries too few bracket sites"; }
  [[ " $VARIANTS " == *" ditsb "*  ]] && { [[ $(count "$W/build-ditsb/tool/bssl" 'd50330ff') -ge 50 ]] || die "ditsb carries too few sb"; }
  # the layout twin must contain NO DIT instruction at all: not a write, and not a read
  # (a `mrs DIT` decodes differently under the two switch models, so its mere presence would
  # break the inert-arm control -- cioparity/blanket_ctor.c)
  if [[ " $VARIANTS " == *" ditnop "* ]]; then
    [[ $(count "$W/build-ditnop/tool/bssl" 'msr[[:space:]]+dit,') -le 1 ]] || die "ditnop still writes DIT"
    [[ $(count "$W/build-ditnop/tool/bssl" 'mrs[[:space:]]+x[0-9]+,[[:space:]]*(dit|s3_3_c4_c2_5)') -eq 0 ]] || die "ditnop still reads DIT"
  fi
  BSRC=""; [[ " $VARIANTS " == *" ditsb "* ]] && BSRC=ditsb
  [[ -z "$BSRC" && " $VARIANTS " == *" ditisb "* ]] && BSRC=ditisb
  if [[ -n "$BSRC" ]]; then
    "$OD" -d "$W/build-$BSRC/tool/bssl" | awk '/^[0-9a-f]+ <.*>:/{fn=$2} /mrs[[:space:]]+x[0-9]+, (DIT|dit|s3_3_c4_c2_5)/{c[fn]++} END{for(f in c) print c[f], f}' | sort -k2 > "$W/bracket_sites.txt"
    info "    bracket sites in $(wc -l < "$W/bracket_sites.txt" | tr -d ' ') functions, from $BSRC ($W/bracket_sites.txt)"
  fi
}
case "${1:-all}" in
  fetch) do_fetch ;; prep) do_prep ;; build) do_build ;;
  all) do_fetch && do_prep && do_build ;;
  *) die "usage: $0 fetch|prep|build|all" ;;
esac
info "done (${1:-all})"
