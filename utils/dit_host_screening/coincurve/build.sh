#!/bin/sh
# Build coincurve (the secp256k1 binding used by web3.py -> eth-account ->
# eth-keys) with the taint pass applied to its vendored libsecp256k1.
# See docs/results/dit-coincurve-real-application.md
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
CLANG="${CLANG:-$HOME/Documents/llvm-project/build/bin/clang}"

git clone --depth 1 https://github.com/ofek/coincurve.git
python3.14 -m venv venv
venv/bin/pip install "hatchling>=1.27.0" "cffi>=2.1.1" "scikit-build-core>=0.9.0"

mkdir -p stubbin && cp "$HERE/pkg-config-stub" stubbin/pkg-config && chmod +x stubbin/pkg-config

cd coincurve
PATH="$PWD/../stubbin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin" \
CC="$CLANG" \
CFLAGS="-ftaint-harden=$HERE/seed.txt" \
COINCURVE_IGNORE_SYSTEM_LIB=ON COINCURVE_SECP256K1_STATIC=ON \
  ../venv/bin/pip install --no-build-isolation .
