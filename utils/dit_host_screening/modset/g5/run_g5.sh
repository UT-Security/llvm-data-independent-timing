#!/bin/bash
# One gem5 run per (binary, mode, workload). gem5 is deterministic, so these can
# run concurrently; only NATIVE silicon runs need an exclusive machine.
set -euo pipefail
D="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$D/../../../.." && pwd)"
G5="${G5:-$REPO/gem5-DIT}"
# gem5.fast for measurement; GEM5_BIN=.../gem5.opt when a run needs --debug-flags
# or the asserts, which .fast compiles out.
GEM5_BIN="${GEM5_BIN:-$G5/build/ARM/gem5.fast}"
BIN=$1; MODE=$2; N=$3; WL=$4; TAG=$5; shift 5
"$GEM5_BIN" -d $D/out/$TAG \
  $G5/configs/example/arm/fdp_neoverse_v2_binary.py --eves --dmp --comp-simp \
  --binary $D/mod_$BIN --arguments "$MODE $N $WL" > $D/out/$TAG.log 2>&1
