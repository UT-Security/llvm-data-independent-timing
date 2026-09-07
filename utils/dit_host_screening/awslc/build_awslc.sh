#!/usr/bin/env bash
# Experiment 13: AWS-LC v5.8.0, the bracket Amazon ships, in three builds of `bssl speed`.
#
#   rel        -DENABLE_DATA_INDEPENDENT_TIMING=OFF        arm A;  arm C = A run with DIT set before main
#   dit        ...=ON, as shipped (mrs; msr dit,#1 ... msr dit,#0 per bracketed entry point)   arm B;  arm H  = B  run with `-dit`
#   ditsb      dit with `sb` after the enable: Apple's recipe                                  arm Bs; arm Hs = Bs run with `-dit`
#
# Every build carries the same speed.cc patch (PMC cycles + instructions per timed loop).
# Built with the platform compiler by default (what AWS's users build with); the taint clang
# is not involved: this experiment measures the library's own bracket, not the pass.
#
#   build_awslc.sh fetch | prep | build | all
# Env: W (default ~/Documents/dit-awslc), CC_BIN/CXX_BIN (default cc/c++), JOBS
set -uo pipefail
RIG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$RIG/../../.." && pwd)"
W="${W:-$HOME/Documents/dit-awslc}"
V=5.8.0; TAG="v$V"; SRC="$W/src/aws-lc-$V"; URL="https://github.com/aws/aws-lc/archive/refs/tags/$TAG.tar.gz"
CC_BIN="${CC_BIN:-cc}"; CXX_BIN="${CXX_BIN:-c++}"; JOBS="${JOBS:-10}"
VARIANTS="rel dit ditsb"
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
OD="$(xcrun -f llvm-objdump 2>/dev/null || command -v objdump)"
count() { "$OD" -d "$1" | grep -ciE "$2"; }

do_fetch() {
  mkdir -p "$W/src"
  [[ -d "$SRC" ]] && { info "source present: $SRC"; return; }
  info "fetching $URL"; curl -fsSL -o "$W/src/aws-lc-$TAG.tar.gz" "$URL" || die "download failed"
  tar xzf "$W/src/aws-lc-$TAG.tar.gz" -C "$W/src" || die "extract failed"
  shasum -a 256 "$W/src/aws-lc-$TAG.tar.gz" | cut -c1-16 | sed 's/^/    tarball sha256 /'
}
do_prep() {
  for v in $VARIANTS; do
    info "tree-$v"; rm -rf "$W/tree-$v"; cp -R "$SRC" "$W/tree-$v"
    python3 "$RIG/patch_speed_pmc.py" "$W/tree-$v" || die "speed patch failed ($v)"
    case $v in rel|dit) ;; *) python3 "$RIG/patch_bracket_variant.py" "$W/tree-$v" "$v" || die "variant patch failed ($v)" ;; esac
  done
  info "libditctl.dylib"; cc -O2 -dynamiclib -o "$W/libditctl.dylib" "$REPO/utils/cio_ditctl.c" || die ditctl
}
do_build() {
  : > "$W/switch_counts.txt"
  for v in $VARIANTS; do
    flag=ON; [[ $v == rel ]] && flag=OFF
    info "build-$v (ENABLE_DATA_INDEPENDENT_TIMING=$flag)"
    cmake -G Ninja -S "$W/tree-$v" -B "$W/build-$v" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DBUILD_TOOL=ON \
        -DENABLE_DATA_INDEPENDENT_TIMING=$flag -DCMAKE_C_COMPILER="$CC_BIN" -DCMAKE_CXX_COMPILER="$CXX_BIN" > "$W/cmake-$v.log" 2>&1 \
      || { tail -20 "$W/cmake-$v.log"; die "cmake failed ($v)"; }
    ninja -C "$W/build-$v" -j"$JOBS" bssl > "$W/ninja-$v.log" 2>&1 || { tail -20 "$W/ninja-$v.log"; die "build failed ($v)"; }
    b="$W/build-$v/tool/bssl"; [[ -x "$b" ]] || die "no bssl for $v"
    # The FIPS module is one translation unit (bcm.c includes every fipsmodule .c), so with the
    # option ON the compiler inlines armv8_set_dit/armv8_restore_dit into every bracketed entry
    # point: one `mrs DIT` and two `msr DIT` per site. With it OFF only the two out-of-line
    # copies remain (the speed tool's -dit path calls them). So the gate counts `mrs DIT` sites.
    echo "$v mrs_dit=$(count "$b" 'mrs\s+x[0-9]+,\s*dit') msr_dit=$(count "$b" 'msr\s+dit,') sb=$(count "$b" '\tsb$') set_dit_calls=$(count "$b" 'bl\s+.*_armv8_set_dit') pmc=$(count "$b" 's3_2_c15_c[01]_0')" | tee -a "$W/switch_counts.txt"
  done
  # gates: what must hold, not numbers to eyeball
  [[ $(count "$W/build-rel/tool/bssl" 'mrs\s+x[0-9]+,\s*dit') -le 2 ]] || die "rel brackets its entry points (the option is not off)"
  [[ $(count "$W/build-dit/tool/bssl" 'mrs\s+x[0-9]+,\s*dit') -ge 50 ]] || die "dit carries too few bracket sites"
  [[ $(count "$W/build-ditsb/tool/bssl" 'mrs\s+x[0-9]+,\s*dit') -ge 50 ]] || die "ditsb carries too few bracket sites"
  [[ $(count "$W/build-dit/tool/bssl" '\tsb$') == 0 ]] || die "dit carries an sb (AWS ships none)"
  [[ $(count "$W/build-ditsb/tool/bssl" '\tsb$') -ge 50 ]] || die "ditsb carries too few sb"
  "$OD" -d "$W/build-dit/tool/bssl" | awk '/^[0-9a-f]+ <.*>:/{fn=$2} /mrs[[:space:]]+x[0-9]+, DIT/{c[fn]++} END{for(f in c) print c[f], f}' | sort -k2 > "$W/bracket_sites.txt"
  info "    bracket sites in $(wc -l < "$W/bracket_sites.txt" | tr -d ' ') functions ($W/bracket_sites.txt)"
  [[ $(count "$W/build-rel/tool/bssl" 's3_2_c15_c[01]_0') -ge 2 ]] || die "speed.cc PMC patch missing from the binary"
}
case "${1:-all}" in
  fetch) do_fetch ;; prep) do_prep ;; build) do_build ;;
  all) do_fetch && do_prep && do_build ;;
  *) die "usage: $0 fetch|prep|build|all" ;;
esac
info "done ($1)"
