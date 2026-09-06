#!/bin/bash
# Add the call-site-mod-set-gate arms to the coincurve rig.
#
# The gate became the compiler default on 2026-08-24, so these arms no longer pass
# a flag for it. The arm NAMES are kept because the published coincurve numbers
# refer to them; an ungated arm now needs -mllvm -taint-no-modset-gate.
#
# Also rebuilds `hoistchk4` with the CURRENT clang and no new flags, as a control:
# the existing venvs were built with build/bin/clang, the new ones with
# build-gfix/bin/clang, and the two must agree byte-for-byte on codegen when the
# gate is off or the arms are not comparable.
set -euo pipefail
SW="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$SW/../.." && pwd)"
CC_PASS="${CC_PASS:-$REPO/build/bin/clang}"
OBJD="$(dirname "$CC_PASS")/llvm-objdump"
[[ -x "$CC_PASS" ]] || { echo "no clang at $CC_PASS - build it (ninja -C <repo>/build clang) or set CC_PASS" >&2; exit 1; }
STUB="$SW/ccbuild/stubbin"
SEED4="$SW/ccbuild/seed4.txt"

build_one() {
  local name="$1"; shift
  local v="$SW/ccbuild/venv_$name"
  echo "=== building $name ==="
  rm -rf "$v"
  /opt/homebrew/bin/python3.14 -m venv "$v"
  "$v/bin/pip" install -q "hatchling>=1.27.0" "cffi>=2.1.1" "scikit-build-core>=0.9.0"
  ( cd "$SW/coincurve" && \
    PATH="$STUB:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin" \
    CC="$CC_PASS" \
    CFLAGS="-ftaint-harden=$SEED4 $*" \
    COINCURVE_IGNORE_SYSTEM_LIB=ON COINCURVE_SECP256K1_STATIC=ON \
    "$v/bin/pip" install --no-build-isolation -q . )
  "$v/bin/pip" install -q eth-account
  local so
  so=$(find "$v" -name "_libsecp256k1*.so" | head -1)
  echo "  $name: $($OBJD -d "$so" | grep -ci 'msr.*dit') MSR DIT"
}

build_one hoistchk4   -mllvm -taint-dit-loop-hoist=1
build_one gated4      -mllvm -taint-dit-loop-hoist=1
build_one clonegated4 -mllvm -taint-dit-loop-hoist=1 \
                      -mllvm -taint-dit-clone-list=$SW/ccbuild/cc_clonelist4.txt
echo "=== CONTROL: hoistchk4 (new clang) must match hoist4 (old clang)"
a=$(find $SW/ccbuild/venv_hoist4    -name "_libsecp256k1*.so" | head -1)
b=$(find $SW/ccbuild/venv_hoistchk4 -name "_libsecp256k1*.so" | head -1)
$OBJD -d "$a" | tail -n +3 > /tmp/cc_a.txt
$OBJD -d "$b" | tail -n +3 > /tmp/cc_b.txt
if cmp -s /tmp/cc_a.txt /tmp/cc_b.txt; then echo "  IDENTICAL - arms comparable"; else
  echo "  !! DIFFER - old and new clang do not agree; do not mix arms"
  diff /tmp/cc_a.txt /tmp/cc_b.txt | head -5; fi
