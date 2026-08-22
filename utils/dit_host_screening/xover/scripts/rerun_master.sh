#!/bin/bash
# Re-take the remaining gem5 sweeps on master. All three were originally measured
# on taint-gem5-bridge, which drained the pipeline at each DIT region exit
# (~170 cyc, removed by PR #72). Toggle cost is what these measure, so they must
# be re-taken. Results go to *_master/ so the pre-fix data stays for comparison.
set -u
X=~/.treehouse/llvm-project-18cdea/2/llvm-project/utils/dit_host_screening/xover
O=~/Documents/dit-crossover; LOG=$O/log
say(){ echo "[$(date '+%H:%M:%S')] $*" | tee -a "$LOG/master.log"; }
for sw in optsweep vsweep rsweep; do
  say "=== $sw on master ==="
  python3 "$X/run_gem5.py" --sweep $sw --jobs 9 --rows 800 --rounds 2 \
      --out "$O/out/gem5/${sw}_master" > "$LOG/gem5_${sw}_master.log" 2>&1
  say "$sw rc=$?"
done
say "ALL MASTER RE-RUNS COMPLETE"
