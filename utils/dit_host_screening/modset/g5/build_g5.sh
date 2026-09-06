#!/bin/bash
# Cross-build the coverage driver + Bitcoin Core's libsecp256k1 for gem5 SE mode.
# usage: build_g5.sh <name> <seedfile> [extra pass flags...]
set -euo pipefail
SW="$(cd "$(dirname "$0")/../.." && pwd)"
REPO="$(cd "$SW/../.." && pwd)"
CL=$HOME/Documents/llvm-project/build-gfix/bin/clang
SR=$HOME/Documents/aarch64-linux-sysroot
G5="${G5:-$REPO/gem5-DIT}"
BTC=$HOME/Documents/bitcoin
D=$SW/modset/g5
NAME=$1; SEED=$2; shift 2

DEFS="-DCOMB_BLOCKS=43 -DCOMB_TEETH=6 -DECMULT_WINDOW_SIZE=15 -DENABLE_MODULE_ELLSWIFT=1 \
-DENABLE_MODULE_EXTRAKEYS=1 -DENABLE_MODULE_MUSIG=1 -DENABLE_MODULE_RECOVERY=1 \
-DENABLE_MODULE_SCHNORRSIG=1 -DENABLE_MODULE_SILENTPAYMENTS=1 \
-DSECP256K1_NO_API_VISIBILITY_ATTRIBUTES"
X="--target=aarch64-linux-gnu --sysroot=$SR -march=armv8.4-a -fuse-ld=/opt/homebrew/bin/ld.lld -static"
INC="-I$BTC/build-hoist/src -I$BTC/src -I$BTC/src/secp256k1/include"

rm -rf $D/obj_$NAME && mkdir -p $D/obj_$NAME
for f in secp256k1 precomputed_ecmult precomputed_ecmult_gen; do
  $CL $X $DEFS $INC -ftaint-harden=$SEED "$@" -O2 -std=c90 -w -fPIC \
     -c $BTC/src/secp256k1/src/$f.c -o $D/obj_$NAME/$f.o
done
# The driver itself is NEVER instrumented: it must not contribute switches.
$CL $X $INC -I$G5/include -O2 -w -c $D/mod_driver.c -o $D/obj_$NAME/mod_driver.o
$CL $X $D/obj_$NAME/*.o $G5/util/m5/build/arm64/out/libm5.a -o $D/mod_$NAME
echo "$NAME: $($HOME/Documents/llvm-project/build-gfix/bin/llvm-objdump -d $D/mod_$NAME | grep -ci 'msr.*dit') MSR DIT"
