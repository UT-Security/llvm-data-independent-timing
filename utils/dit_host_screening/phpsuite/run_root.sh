#!/usr/bin/env bash
# Experiment 11 under root: Spotlight off, php-cgi pinned to a P-core, PMC cycles
# and instructions per request. Everything else is run_suite.sh unchanged.
#
#   sudo utils/dit_host_screening/phpsuite/run_root.sh
#
# WHY ROOT, when the rest of this experiment deliberately needs none. The metric
# is Apple's fixed performance counters read from EL0 (see cio_ditctl.c), and
# those are PER-CORE registers: a process that migrates between its two
# snapshots differences two different cores, and one whose core is shared with
# something else is charged that something else's cycles. bench.py detects both
# and drops the sample, so an unrooted run is still correct -- it just pays in
# discarded requests. Root fixes the causes instead:
#
#   kern.sched_thread_bind_cpu   binds the thread, so nothing migrates. It is a
#                                development-kernel facility (boot-args
#                                enable_skstb=1) and writing it is root-only.
#   mdutil -i off                stops Spotlight, which wakes up to index five
#                                freshly built PHP trees and saturates all ten
#                                cores. Measured mid-storm: 47% of samples
#                                dropped. Re-enabled on the way out.
#
# Both are recorded with the results: every row prints its pinned state and its
# dropped-sample count, so a run that did NOT get them is visible in its own
# output rather than only in the shell history.
#
# Env: W, WARMUP, MEASURED, DB_PORT as run_suite.sh; SUDO_USER is used to hand
# the results back, so invoke through sudo rather than from a root shell.
set -uo pipefail
RIG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[[ $EUID -eq 0 ]] || { echo "run me with sudo: sudo $0" >&2; exit 1; }
OWNER="${SUDO_USER:-$(stat -f %Su /dev/console)}"
export HOME="$(dscl . -read "/Users/$OWNER" NFSHomeDirectory | awk '{print $2}')"
export W="${W:-$HOME/Documents/dit-phpsuite}"
export DB_PORT="${DB_PORT:-3307}" WARMUP="${WARMUP:-50}" MEASURED="${MEASURED:-100}"
mkdir -p "$W/results"
LOG="$W/results/run.log"

cleanup() {
  echo "==> re-enabling Spotlight"
  mdutil -i on / >/dev/null 2>&1
  chown -R "$OWNER" "$W" 2>/dev/null
  echo "==> results in $W/results (owner $OWNER)"
}
trap cleanup EXIT

echo "==> stopping Spotlight indexing"
mdutil -i off / 2>&1 | sed 's/^/    /'

# The build leaves ~9GB of PHP trees in the page cache and pushes a little into
# swap at -j10. None of that is a leak and none of it is under pressure, but it
# leaves the free list short, and a page-in landing inside a measured request is
# a fat outlier that no amount of rotation averages away. Dropping the cache
# costs nothing here: every row runs 50 warm-up requests before it measures, and
# they refill exactly the pages the benchmark touches.
echo "==> purging the file cache"
purge 2>/dev/null
vm_stat | awk '/Pages free/ {printf "    %.1f GB free after purge\n", $3 * 16384 / 1073741824}'

# Preflight, because both root-only facilities fail SILENTLY when they fail:
# an unpatched kernel gives pmc=0 and a refused bind gives pinned=-1, and either
# would otherwise only show up 30 minutes later in the rows.
echo "==> preflight: counters, injection and pinning under root"
printf 'int main(void){volatile double s=0;for(long i=1;i<8000000L;i++)s+=1.0/i;return 0;}' > "$W/_preflight.c"
cc -O2 -o "$W/_preflight" "$W/_preflight.c" || { echo "preflight build failed" >&2; exit 1; }
out=$(DYLD_INSERT_LIBRARIES="$W/libditctl.dylib" ENABLE_DIT=0 "$W/_preflight" 2>&1 >/dev/null)
echo "    $out"
rm -f "$W/_preflight" "$W/_preflight.c"
[[ "$out" == *"pmc=1"* ]]     || { echo "PMCs unreadable from EL0: this kernel is not patched" >&2; exit 1; }
[[ "$out" == *"pinned=-1"* ]] && echo "    WARNING: the thread bind was refused; the run will filter migrations instead of preventing them"

# Named workloads re-run just those rows: `sudo run_root.sh wpapi` after a
# failure, rather than repeating the three that already succeeded.
export ROWS="${*:-${ROWS:-zend symfony wordpress wpapi}}"
echo "==> suite [$ROWS] (WARMUP=$WARMUP MEASURED=$MEASURED)"
"$RIG/run_suite.sh" 2>&1 | tee -a "$LOG"
