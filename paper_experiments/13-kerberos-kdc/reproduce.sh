#!/bin/bash
# Reproduce experiment 13 (MIT Kerberos KDC: blanket vs the developer's bracket) on any
# Apple Silicon Mac. Nothing runs as root; the KDC listens on 127.0.0.1:8888 only.
#
#   ./reproduce.sh            every stage, in order
#   ./reproduce.sh clang      build the taint compiler at the checked-out commit (skipped if LLVM_BUILD has one)
#   ./reproduce.sh build      krb5 1.22.2 downloaded from kerberos.org, builtin C crypto, bracket wrapped inert;
#                             libk5crypto rebuilt twice for arms B and Bn; libditctl.dylib   (~5 min)
#   ./reproduce.sh realm      a throwaway realm DIT.TEST with 20 hammer principals, a user and a service
#   ./reproduce.sh run        idle, TGS-REQ-only and AS+TGS rows, 4 arms x (2 warm-up + 15 reps)   (~15 min, idle machine)
#   ./reproduce.sh collect    copy results and a provenance record into data/
#
# Env: LLVM_BUILD (default <repo>/build), W (work dir, default ~/Documents/dit-krb5),
#      REPS/WARM (15/2), TGS_N (10000), HAMMER_N/HAMMER_R (20/60), JOBS (10)
set -euo pipefail
E="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
R="$(cd "$E/../.." && pwd)"
RIG="$R/utils/dit_host_screening/krb5kdc"
export W="${W:-$HOME/Documents/dit-krb5}"
export LLVM_BUILD="${LLVM_BUILD:-$R/build}" JOBS="${JOBS:-10}"
export REPS="${REPS:-15}" WARM="${WARM:-2}" TGS_N="${TGS_N:-10000}" HAMMER_N="${HAMMER_N:-20}" HAMMER_R="${HAMMER_R:-60}"
STAGES="${*:-clang build realm run collect}"
want() { [[ " $STAGES " == *" $1 "* ]]; }
info() { printf '\033[1m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
[[ "$(uname -s)/$(uname -m)" == Darwin/arm64 ]] || die "experiment 13 runs on Apple Silicon (PSTATE.DIT, macOS)"

if want clang; then
  if [[ -x "$LLVM_BUILD/bin/clang" && -x "$LLVM_BUILD/bin/llvm-objdump" ]]; then
    info "clang: using $LLVM_BUILD"
  else
    info "clang: configuring and building the taint compiler in $LLVM_BUILD (about an hour)"
    command -v cmake >/dev/null || brew install cmake; command -v ninja >/dev/null || brew install ninja
    cmake -G Ninja -S "$R/llvm" -B "$LLVM_BUILD" -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=AArch64 \
      -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON > "$LLVM_BUILD.cmake.log" 2>&1 || die "cmake failed"
    ninja -C "$LLVM_BUILD" clang llvm-objdump llvm-nm LTO || die "ninja failed"
  fi
  printf -- '-isysroot %s\n' "$(xcrun --show-sdk-path)" > "$LLVM_BUILD/bin/clang.cfg"
fi
if want build; then info "build"; "$RIG/build_krb5.sh" all; fi
if want realm; then info "realm"; "$RIG/setup_realm.sh"; fi
if want run; then
  info "run (idle, tgs, mix)"; mkdir -p "$W/results"
  ( cd "$W" && python3 "$RIG/bench_kdc.py" idle tgs mix | tee "$W/results/run.txt" )
fi
if want collect; then
  info "collect -> $E/data"
  cp "$W/results/run.txt" "$E/data/run.txt" 2>/dev/null || cp "$W/results/run1.txt" "$E/data/run.txt"
  cp "$W"/results/kdc_*.json "$E/data/" 2>/dev/null || true
  ( cd "$W/build-base" && for f in lib/crypto/krb/dit_bracket.h $(grep -l 'dit_bracket.h' lib/crypto/krb/*.c); do
      diff -u <(tar -xOzf "$W/src/krb5-1.22.2.tar.gz" "krb5-1.22.2/src/$f" 2>/dev/null) "$f" | sed "1s|.*|--- krb5-1.22.2/src/$f|; 2s|.*|+++ $f|"; done ) > "$E/data/bracket.diff" || true
  LB="$LLVM_BUILD/bin"; lib=$(ls "$W"/lib-bracket/libk5crypto.*.*.dylib | head -1)
  {
    echo "== $(date '+%Y-%m-%d %H:%M %Z')  host $(sysctl -n hw.model) $(sysctl -n machdep.cpu.brand_string)  macOS $(sw_vers -productVersion)"
    echo "compiler: $(git -C "$R" rev-parse HEAD)$(git -C "$R" diff --quiet || echo ' +uncommitted')  $("$LB/clang" --version | head -1)"
    echo "krb5: 1.22.2 from $(shasum -a 256 "$W/src/krb5-1.22.2.tar.gz" | cut -c1-16)...  builtin crypto, tls=no, pkinit off, db2"
    echo "bracket libk5crypto: msr DIT $("$LB/llvm-objdump" -d "$lib" | grep -ciE '\bmsr\b\s+dit,'), sb $("$LB/llvm-objdump" -d "$lib" | grep -cE '\tsb$'), mrs DIT $("$LB/llvm-objdump" -d "$lib" | grep -ciE '\bmrs\b.*\bdit\b'); NOP twin identical symbol addresses: $(cmp -s <("$LB/llvm-nm" "$lib" | awk '{print $1,$3}') <("$LB/llvm-nm" "$W"/lib-bracketnop/libk5crypto.*.*.dylib | awk '{print $1,$3}') && echo yes || echo NO)"
    echo "reps/warm: $REPS/$WARM  TGS_N $TGS_N  hammer ${HAMMER_N}x${HAMMER_R}"
  } >> "$E/data/provenance.txt"
  tail -5 "$E/data/provenance.txt"
fi
info "done: $STAGES"
