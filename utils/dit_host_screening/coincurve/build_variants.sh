#!/bin/bash
# Build coincurve three ways, each into its own venv, for the timing measurement.
#
#   nodit   -ftaint-harden=<EMPTY seed>  -> 0 MSR DIT. THE BASELINE.
#           Not a stock build: it goes through the same MIR round-trip as the
#           instrumented ones, so the codegen lottery (trap 7) cannot be charged
#           to DIT. Comparing to a plain -O2 build would be wrong.
#   region  -ftaint-harden=<real seed>   -> shipped default placement
#   hoist   + -mllvm -taint-dit-loop-hoist=1
#
# The always-on arm needs no build: DYLD_INSERT_LIBRARIES on the nodit venv.
set -euo pipefail
SW="$(cd "$(dirname "$0")/.." && pwd)"
CC_PASS="${CC_PASS:-$HOME/Documents/llvm-project/build/bin/clang}"
STUB="$SW/ccbuild/stubbin"
SEED_REAL="$SW/ccbuild/seed.txt"
SEED_EMPTY="$SW/ccbuild/seed_empty.txt"
: > "$SEED_EMPTY"

build_one() {
  local name="$1" seed="$2" extra="${3:-}"
  local v="$SW/ccbuild/venv_$name"
  echo "=== building $name ==="
  rm -rf "$v"
  /opt/homebrew/bin/python3.14 -m venv "$v"
  "$v/bin/pip" install -q "hatchling>=1.27.0" "cffi>=2.1.1" "scikit-build-core>=0.9.0"
  ( cd "$SW/coincurve" && \
    PATH="$STUB:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin" \
    CC="$CC_PASS" \
    CFLAGS="-ftaint-harden=$seed $extra" \
    COINCURVE_IGNORE_SYSTEM_LIB=ON COINCURVE_SECP256K1_STATIC=ON \
    "$v/bin/pip" install --no-build-isolation -q . )
  "$v/bin/pip" install -q eth-account
  local so
  so=$(find "$v" -name "_libsecp256k1*.so" | head -1)
  echo "  $name: $("$HOME/Documents/llvm-project/build/bin/llvm-objdump" -d "$so" | grep -ci 'msr.*dit') MSR DIT"
}

build_one nodit  "$SEED_EMPTY"
build_one region "$SEED_REAL"
build_one hoist  "$SEED_REAL" "-mllvm -taint-dit-loop-hoist=1"
echo "ALL VARIANTS BUILT"
