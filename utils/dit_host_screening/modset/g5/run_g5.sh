#!/bin/bash
# One gem5 run per (binary, mode, workload). gem5 is deterministic, so these can
# run concurrently; only NATIVE silicon runs need an exclusive machine.
set -euo pipefail
D="$(cd "$(dirname "$0")" && pwd)"
G5=$HOME/Documents/gem5-DIT
BIN=$1; MODE=$2; N=$3; WL=$4; TAG=$5; shift 5
$G5/build/ARM/gem5.opt -d $D/out/$TAG \
  $G5/configs/example/arm/fdp_neoverse_v2_binary.py --eves --dmp --comp-simp \
  --binary $D/mod_$BIN --arguments "$MODE $N $WL" > $D/out/$TAG.log 2>&1
