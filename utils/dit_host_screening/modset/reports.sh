#!/bin/bash
set -euo pipefail
SW="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$SW/../.." && pwd)"
CL="${CL:-$REPO/build/bin/clang}"
[[ -x "$CL" ]] || { echo "no clang at $CL - build it (ninja -C <repo>/build clang) or set CL" >&2; exit 1; }
BTC=$HOME/Documents/bitcoin
SEED=$SW/btc/seed9.txt
NAME=$1; shift
DEFS="-DCOMB_BLOCKS=43 -DCOMB_TEETH=6 -DECMULT_WINDOW_SIZE=15 -DENABLE_MODULE_ELLSWIFT=1 \
-DENABLE_MODULE_EXTRAKEYS=1 -DENABLE_MODULE_MUSIG=1 -DENABLE_MODULE_RECOVERY=1 \
-DENABLE_MODULE_SCHNORRSIG=1 -DENABLE_MODULE_SILENTPAYMENTS=1 \
-DSECP256K1_NO_API_VISIBILITY_ATTRIBUTES"
$CL $DEFS -I$BTC/build-hoist/src -I$BTC/src \
  -ftaint-harden=$SEED "$@" \
  -mllvm -taint-uncovered-report=$SW/modset/$NAME.uncovered.txt \
  -mllvm -taint-clobber-report=$SW/modset/$NAME.clobber.txt \
  -mllvm -taint-callsite-report=$SW/modset/$NAME.callsite.txt \
  -O2 -std=c90 -arch arm64 -fPIC -fvisibility=hidden -w \
  -c $BTC/src/secp256k1/src/secp256k1.c -o /dev/null
wc -l < $SW/modset/$NAME.uncovered.txt | xargs echo "$NAME uncovered lines:"
