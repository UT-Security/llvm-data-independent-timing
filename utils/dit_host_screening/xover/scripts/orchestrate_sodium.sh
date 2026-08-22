#!/bin/bash
# libsodium composite sweeps: two public lanes x three grids. Native, exclusive.
set -u
X=~/.treehouse/llvm-project-18cdea/2/llvm-project/utils/dit_host_screening/xover
LOG=~/Documents/dit-crossover/log
say(){ echo "[$(date '+%H:%M:%S')] $*" | tee -a "$LOG/sodium.log"; }
for lane in sqlite lua; do
  for grid in R f prim; do
    say "=== $lane / $grid ==="
    python3 "$X/run_sodium.py" --lane $lane --grid $grid --reps 20 --burnin 3 \
        --rounds 60 --depth 15 > "$LOG/sodium_${lane}_${grid}.log" 2>&1
    say "$lane/$grid rc=$?"
  done
done
say "SODIUM SWEEPS COMPLETE"
