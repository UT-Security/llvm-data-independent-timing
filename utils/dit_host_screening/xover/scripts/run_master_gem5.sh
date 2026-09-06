#!/bin/bash
# Re-run the gem5 crossover sweep on MASTER.
#
# WHY: every gem5 number taken overnight came from taint-gem5-bridge, which
# predates PR #72 (dit/no-drain-on-region-exit). On that branch `msr dit, #0`
# carried IsNonSpeculative while DitCC was an implicit source of nearly every
# gated instruction, so consumers stalled in the IQ behind it - approximating a
# pipeline drain at each region exit, documented at ~170 cycles. Master removes
# it. Toggle cost is exactly what this evaluation measures, so the earlier gem5
# figures overstate it and must be re-taken. The native M5 numbers are unaffected:
# that is real silicon, which genuinely serialises the write.
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
X=$REPO/utils/dit_host_screening/xover
G=${G5:-$REPO/gem5-DIT}; O=~/Documents/dit-crossover; LOG=$O/log
say(){ echo "[$(date '+%H:%M:%S')] $*" | tee -a "$LOG/master.log"; }

say "waiting for the gem5 build"
while pgrep -f 'scons' >/dev/null 2>&1; do sleep 60; done
GEM5_BIN="${GEM5_BIN:-$G/build/ARM/gem5.fast}"   # .opt only for --debug-flags/asserts
[ -x "$GEM5_BIN" ] || { say "!! no gem5 binary at $GEM5_BIN"; exit 1; }
say "built: $(date -r "$GEM5_BIN" '+%b %d %H:%M')  branch=$(git -C $G branch --show-current)"

say "smoke test"
D=/tmp/master_smoke; rm -rf $D; mkdir -p $D
"$GEM5_BIN" -d $D "$G/configs/example/arm/fdp_neoverse_v2_binary.py" \
   --eves --dmp --comp-simp --binary $O/build/gem5/xover_nodit \
   --arguments "0 800 2 1 40 0" > $D/run.log 2>&1
if ! grep -q 'WORK sqlite' $D/run.log; then say "!! smoke test failed"; tail -5 $D/run.log; exit 1; fi
say "smoke ok: $(awk '/Begin Simulation/{n++} n==1 && /^simInsts/{print "simInsts="$2}' $D/stats.txt | head -1)"

say "=== GEM5 fsweep on master (6 arms incl. swcyc30, both switch models) ==="
python3 "$X/run_gem5.py" --sweep fsweep --jobs 9 --rows 800 --rounds 2 \
    --out "$O/out/gem5/fsweep_master" > "$LOG/gem5_fsweep_master.log" 2>&1
say "fsweep rc=$?"
say "MASTER SWEEP COMPLETE"
