#!/bin/bash
set -euo pipefail
D="$(cd "$(dirname "$0")" && pwd)"
G5=$HOME/Documents/gem5-DIT
BIN=$1; MODE=$2; N=$3; WL=$4; TAG=$5
$G5/build/ARM/gem5.opt -d $D/out/$TAG \
  $G5/configs/example/arm/fdp_neoverse_v2_binary.py --eves --dmp --comp-simp \
  --no-speculative-dit \
  --binary $D/mod_$BIN --arguments "$MODE $N $WL" > $D/out/$TAG.log 2>&1
