#!/bin/bash
# Fast A/B loop for the call-site mod-set gate (the compiler default since
# 2026-08-24; pass -mllvm -taint-no-modset-gate for the ungated arm) on Bitcoin
# Core's vendored
# libsecp256k1. Compiles the single TU that carries every DIT switch
# (src/secp256k1.c) with the EXACT flags Bitcoin Core's build-hoist used, so the
# switch counts are directly comparable to the +51% ConnectBlockAllEcdsa result.
set -euo pipefail
SW="$(cd "$(dirname "$0")/.." && pwd)"
CL="${CL:-$HOME/Documents/llvm-project/build-gfix/bin/clang}"
BTC=$HOME/Documents/bitcoin
SEED=$SW/btc/seed9.txt
NAME=$1; shift
DEFS="-DCOMB_BLOCKS=43 -DCOMB_TEETH=6 -DECMULT_WINDOW_SIZE=15 -DENABLE_MODULE_ELLSWIFT=1 \
-DENABLE_MODULE_EXTRAKEYS=1 -DENABLE_MODULE_MUSIG=1 -DENABLE_MODULE_RECOVERY=1 \
-DENABLE_MODULE_SCHNORRSIG=1 -DENABLE_MODULE_SILENTPAYMENTS=1 \
-DSECP256K1_NO_API_VISIBILITY_ATTRIBUTES"
$CL $DEFS -I$BTC/build-hoist/src -I$BTC/src \
  -ftaint-harden=$SEED "$@" \
  -O2 -std=c90 -arch arm64 -fPIC -fvisibility=hidden -w \
  -c $BTC/src/secp256k1/src/secp256k1.c -o $SW/modset/$NAME.o
echo "$NAME: $($HOME/Documents/llvm-project/build-gfix/bin/llvm-objdump -d $SW/modset/$NAME.o | grep -ci 'msr.*dit') MSR DIT"
